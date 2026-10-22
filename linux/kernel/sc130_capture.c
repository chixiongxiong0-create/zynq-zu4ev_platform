// SPDX-License-Identifier: GPL-2.0
/* Experimental dual physical-master capture. No hardware throughput claim. */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include <linux/dma/xilinx_dma.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/workqueue.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>
#include "dual_core.h"
#include "sc130_contract.h"
#define W 1280
#define H 1024
#define STRIDE 2560
#define SIZE (STRIDE*H)
enum { ID=0, VERSION=4, CONTROL=8, STATUS=0x0c, ARM_EPOCH=0x10,
    RESULT_EPOCH=0x14, FLAGS=0x18, ACK=0x1c, INPUT_SEQ=0x20, SEQ=0x24,
    WORDS=0x28, LINES=0x2c, IRQ_ENABLE=0x3c, CAP=0x40, PHASE=0x44,
    MISS=0x48, ACCEPT=0x4c, TARGET=0x50, TICK_LO=0x54, TICK_HI=0x58,
    ARM_LO=0x5c, ARM_HI=0x60, SOF_LO=0x64, SOF_HI=0x68,
    DONE_LO=0x6c, DONE_HI=0x70, GUARD=0x74, BYTES=0x78 };
#define ARM 1
#define ABORT 2
struct capture;
struct channel;
struct callback_ctx { struct channel *ch; u32 generation,epoch; };
struct buffer {
    struct vb2_v4l2_buffer vb;
    struct dma_buf *pin;
    struct dma_buf_attachment *attach[2];
    struct sg_table *sgt[2];
    dma_addr_t address[2];
};
struct channel {
    struct capture *s;
    unsigned index;
    struct dma_chan *dma;
    void __iomem *gate;
    int irq;
    struct work_struct work;
    struct delayed_work timeout;
    struct callback_ctx *ctx;
    bool timedout;
    u64 frames,errors;
    u32 miss_start,miss_end;
};
struct capture {
    struct v4l2_device v4l2;
    struct video_device video;
    struct vb2_queue queue;
    struct mutex mutex;
    spinlock_t lock; /* owns core and serializes STOPPING against actual ARM MMIO */
    struct dual_core core;
    struct buffer *buffers[DUAL_MAX];
    struct channel ch[2];
    struct workqueue_struct *workers;
    struct delayed_work publish;
    bool fatal,disconnected;
    u64 late,tick_base,ns_base;
};
static bool trace_frames;
module_param(trace_frames,bool,0644);
MODULE_PARM_DESC(trace_frames,"Per-frame hardware/software trace (can perturb timing)");
static u64 tick64(struct channel *ch,unsigned lo,unsigned hi)
{ u32 low=readl(ch->gate+lo);return ((u64)readl(ch->gate+hi)<<32)|low; }
static struct buffer *as_buffer(struct vb2_buffer *vb)
{ return container_of(to_vb2_v4l2_buffer(vb),struct buffer,vb); }
static int halted(struct channel *ch)
{
    int value,ret=read_poll_timeout(xilinx_vdma_channel_is_halted,value,value!=0,10,100000,false,ch->dma);
    return ret?ret:(value==1?0:-EIO);
}
static void fatal_stop(struct capture *s)
{
    unsigned long f;spin_lock_irqsave(&s->lock,f);
    s->fatal=true;
    if(!s->core.stopping)for(unsigned i=0;i<2;i++)s->ch[i].miss_end=readl(s->ch[i].gate+MISS);
    dual_stop(&s->core);
    for(unsigned i=0;i<2;i++)if(s->ch[i].gate) {
        writel(0,s->ch[i].gate+IRQ_ENABLE);writel(ABORT,s->ch[i].gate+CONTROL);
    }
    spin_unlock_irqrestore(&s->lock,f);vb2_queue_error(&s->queue);
}
static int cancel_channel(struct channel *ch)
{
    struct capture *s=ch->s;u32 value;
    writel(0,ch->gate+IRQ_ENABLE);writel(ABORT,ch->gate+CONTROL);
    int ret=dmaengine_terminate_sync(ch->dma);
    if(ret || halted(ch))goto fatal;
    ret=readl_poll_timeout(ch->gate+GUARD,value,value&BIT(2),10,100000);
    if(ret)goto fatal;
    ret=readl_poll_timeout(ch->gate+STATUS,value,!(value&(BIT(1)|BIT(2))),10,100000);
    if(ret)goto fatal;
    writel(readl(ch->gate+RESULT_EPOCH),ch->gate+ACK);
    return 0;
fatal:
    dev_crit(s->v4l2.dev,"channel%u not quiescent; quarantine both mappings until reboot\n",ch->index);
    fatal_stop(s);return -EIO;
}
static void dma_done(void *arg)
{
    struct callback_ctx *cb=arg;
    struct channel *ch=cb->ch;
    struct capture *s=ch->s;
    u32 gen=cb->generation,epoch=cb->epoch;
    unsigned long f;spin_lock_irqsave(&s->lock,f);
    if(!dual_dma_done(&s->core,ch->index,gen,epoch))queue_work(s->workers,&ch->work);
    spin_unlock_irqrestore(&s->lock,f);
}
static irqreturn_t gate_irq(int irq,void *arg)
{
    struct channel *ch=arg;struct capture *s=ch->s;unsigned long f;
    if(!(readl(ch->gate+STATUS)&BIT(3)))return IRQ_NONE;
    writel(0,ch->gate+IRQ_ENABLE);
    spin_lock_irqsave(&s->lock,f);
    if(!s->core.stopping)queue_work(s->workers,&ch->work);
    spin_unlock_irqrestore(&s->lock,f);return IRQ_HANDLED;
}
static void timeout_work(struct work_struct *work)
{
    struct channel *ch=container_of(to_delayed_work(work),struct channel,timeout);
    struct capture *s=ch->s;unsigned long f;
    spin_lock_irqsave(&s->lock,f);
    if(!s->core.stopping && s->core.ch[ch->index].id>=0) {
        ch->timedout=true;queue_work(s->workers,&ch->work);
    }
    spin_unlock_irqrestore(&s->lock,f);
}
static void publish_work(struct work_struct *work)
{
    struct capture *s=container_of(to_delayed_work(work),struct capture,publish);
    unsigned long f;int ids[DUAL_MAX],n=0,id;bool pending=false;
    spin_lock_irqsave(&s->lock,f);
    while(n<DUAL_MAX && (id=dual_pop(&s->core,ktime_get_ns()))>=0)ids[n++]=id;
    if(!s->core.stopping)for(unsigned i=0;i<s->core.count;i++)pending|=s->core.slots[i].state==D_READY;
    spin_unlock_irqrestore(&s->lock,f);
    for(int i=0;i<n;i++)vb2_buffer_done(&s->buffers[ids[i]]->vb.vb2_buf,VB2_BUF_STATE_DONE);
    if(pending) {
        spin_lock_irqsave(&s->lock,f);
        if(!s->core.stopping)queue_delayed_work(s->workers,&s->publish,msecs_to_jiffies(1));
        spin_unlock_irqrestore(&s->lock,f);
    }
}
static void channel_work(struct work_struct *work)
{
    struct channel *ch=container_of(work,struct channel,work);
    struct capture *s=ch->s;unsigned ci=ch->index;unsigned long f;
    int id;u32 epoch,gen;bool dd,expired;
    spin_lock_irqsave(&s->lock,f);
    if(s->core.stopping){spin_unlock_irqrestore(&s->lock,f);return;}
    id=s->core.ch[ci].id;epoch=s->core.ch[ci].epoch;gen=s->core.generation;
    dd=s->core.ch[ci].dma_done;expired=ch->timedout;
    spin_unlock_irqrestore(&s->lock,f);
    u32 status=readl(ch->gate+STATUS),flags=readl(ch->gate+FLAGS),re=readl(ch->gate+RESULT_EPOCH);
    bool rv=status&BIT(3);
    if(id>=0 && sc130_result_ready(dd,rv,expired,flags,re,epoch)) {
        cancel_delayed_work_sync(&ch->timeout);
        u32 seq=readl(ch->gate+SEQ),guard=readl(ch->gate+GUARD),bytes=readl(ch->gate+BYTES);
        u64 sof=tick64(ch,SOF_LO,SOF_HI),done=tick64(ch,DONE_LO,DONE_HI),stamp=0;
        bool good=sc130_result_good(dd,rv,expired,flags,re,epoch,readl(ch->gate+WORDS),readl(ch->gate+LINES)) &&
                  dual_payload_good(ci,seq,guard,bytes);
        if(dual_sof_timestamp(sof,s->tick_base,s->ns_base,&stamp))good=false;
        if(good && halted(ch))good=false;
        /* Re-sample guard after HALTED: an early empty count is not a final barrier. */
        if(good) {
            guard=readl(ch->gate+GUARD);bytes=readl(ch->gate+BYTES);
            good=dual_payload_good(ci,seq,guard,bytes) && readl(ch->gate+FLAGS)==1;
        }
        int stopped=0;
        if(good)writel(epoch,ch->gate+ACK);else {stopped=cancel_channel(ch);ch->errors++;}
        if(!stopped){kfree(ch->ctx);ch->ctx=NULL;}
        struct buffer *b=s->buffers[id];
        if(!s->fatal)dma_sync_sgtable_for_cpu(ch->dma->device->dev,b->sgt[ci],DMA_FROM_DEVICE);
        b->vb.sequence=seq;b->vb.field=V4L2_FIELD_NONE;
        /* V4L2 EOF is software capture completion, not sensor exposure or PL SOF. */
        b->vb.vb2_buf.timestamp=ktime_get_ns();
        vb2_set_plane_payload(&b->vb.vb2_buf,0,good?SIZE:0);
        spin_lock_irqsave(&s->lock,f);
        int error_id=-1;
        if(dual_matches(&s->core,ci,gen,epoch)) {
            error_id=dual_complete(&s->core,ci,seq,ktime_get_ns(),good);
            if(error_id>=0 && good)s->late++;
        }
        ch->timedout=false;
        if(!s->core.stopping)queue_delayed_work(s->workers,&s->publish,0);
        spin_unlock_irqrestore(&s->lock,f);
        if(error_id>=0)vb2_buffer_done(&b->vb.vb2_buf,VB2_BUF_STATE_ERROR);
        ch->frames++;
        if(trace_frames)dev_info(s->v4l2.dev,"ch%u seq=%u epoch=%u sof_tick=%llu sof_est_ns=%llu done_tick=%llu worker_ns=%llu guard=%x bytes=%u valid=%u\n",
            ci,seq,epoch,sof,stamp,done,ktime_get_ns(),guard,bytes,good);
    }
    spin_lock_irqsave(&s->lock,f);id=dual_begin(&s->core,ci);
    if(id<0){spin_unlock_irqrestore(&s->lock,f);return;}
    epoch=s->core.ch[ci].epoch;gen=s->core.generation;
    spin_unlock_irqrestore(&s->lock,f);
    struct callback_ctx *cb=kmalloc(sizeof(*cb),GFP_KERNEL);
    if(!cb)goto failed;
    *cb=(struct callback_ctx){ch,gen,epoch};ch->ctx=cb;
    struct buffer *b=s->buffers[id];
    dma_sync_sgtable_for_device(ch->dma->device->dev,b->sgt[ci],DMA_FROM_DEVICE);
    struct xilinx_vdma_config config={.frm_cnt_en=1,.coalesc=1,.park=0};
    if(xilinx_vdma_channel_set_config(ch->dma,&config))goto failed;
    struct { struct dma_interleaved_template xt;struct data_chunk chunk; } x={
        .xt={.dir=DMA_DEV_TO_MEM,.dst_sgl=true,.dst_inc=true,.frame_size=1,.numf=H},
        .chunk={.size=STRIDE}};
    x.xt.dst_start=b->address[ci];
    struct dma_async_tx_descriptor *desc=dmaengine_prep_interleaved_dma(ch->dma,&x.xt,DMA_PREP_INTERRUPT|DMA_CTRL_ACK);
    if(!desc)goto failed;
    desc->callback=dma_done;desc->callback_param=cb;
    if(dma_submit_error(dmaengine_submit(desc)))goto failed;
    writel(epoch,ch->gate+ARM_EPOCH);
    dma_async_issue_pending(ch->dma);
    /* Every actual ARM is inside the same lock as STOPPING+both ABORT writes. */
    bool armed=false;
    spin_lock_irqsave(&s->lock,f);
    if(dual_matches(&s->core,ci,gen,epoch)) {
        writel(1,ch->gate+IRQ_ENABLE);writel(ARM,ch->gate+CONTROL);
        mod_delayed_work(s->workers,&ch->timeout,msecs_to_jiffies(250));
        armed=true;
    }
    spin_unlock_irqrestore(&s->lock,f);
    if(armed) {
        u32 accepted;
        if(readl_poll_timeout(ch->gate+ACCEPT,accepted,accepted==epoch,10,1000) ||
           (readl(ch->gate+TARGET)&1)!=ci)goto failed;
    }
    if(trace_frames)dev_info(s->v4l2.dev,"arm ch%u requested=%u accepted=%u target=%u arm_tick=%llu tick=%llu ns=%llu miss=%u\n",
        ci,epoch,readl(ch->gate+ACCEPT),readl(ch->gate+TARGET),tick64(ch,ARM_LO,ARM_HI),
        tick64(ch,TICK_LO,TICK_HI),ktime_get_ns(),readl(ch->gate+MISS));
    return;
failed:
    spin_lock_irqsave(&s->lock,f);
    if(dual_matches(&s->core,ci,gen,epoch)){ch->timedout=true;queue_work(s->workers,&ch->work);}
    spin_unlock_irqrestore(&s->lock,f);
}
static void release_mapping(struct capture *s,struct buffer *b)
{
    if(s->fatal) {
        dev_crit(s->v4l2.dev,"quarantine DMA-BUF plus both master mappings until reboot\n");
        return;
    }
    for(unsigned i=0;i<2;i++) {
        if(b->sgt[i]){dma_buf_unmap_attachment_unlocked(b->attach[i],b->sgt[i],DMA_FROM_DEVICE);b->sgt[i]=NULL;}
        if(b->attach[i]){dma_buf_detach(b->pin,b->attach[i]);b->attach[i]=NULL;}
    }
    if(b->pin){dma_buf_put(b->pin);b->pin=NULL;}
}
static int prepare_buffer(struct vb2_buffer *vb)
{
    struct capture *s=vb2_get_drv_priv(vb->vb2_queue);struct buffer *b=as_buffer(vb);
    if(s->fatal || s->disconnected)return -EIO;
    if(vb2_plane_size(vb,0)<SIZE || vb->index>=DUAL_MAX)return -EINVAL;
    if(!b->pin) {
        b->pin=vb2_dma_contig_memops.get_dmabuf(vb,vb->planes[0].mem_priv,O_RDWR);
        int ret=sc130_export_status((unsigned long)b->pin);
        if(ret){b->pin=NULL;return ret;}
        for(unsigned i=0;i<2;i++) {
            b->attach[i]=dma_buf_attach(b->pin,s->ch[i].dma->device->dev);
            if(IS_ERR(b->attach[i])){b->attach[i]=NULL;goto error;}
            b->sgt[i]=dma_buf_map_attachment_unlocked(b->attach[i],DMA_FROM_DEVICE);
            if(IS_ERR(b->sgt[i])){b->sgt[i]=NULL;goto error;}
            struct scatterlist *sg;unsigned j;dma_addr_t end=sg_dma_address(b->sgt[i]->sgl);
            b->address[i]=end;
            for_each_sg(b->sgt[i]->sgl,sg,b->sgt[i]->nents,j) {
                if(sg_dma_address(sg)!=end)goto error;
                end+=sg_dma_len(sg);
            }
            if(end-b->address[i]<SIZE || b->address[i]>U32_MAX || SIZE-1>U32_MAX-b->address[i])goto error;
        }
    }
    s->buffers[vb->index]=b;vb2_set_plane_payload(vb,0,SIZE);return 0;
error:release_mapping(s,b);return -EIO;
}
static void cleanup_buffer(struct vb2_buffer *vb)
{
    struct capture *s=vb2_get_drv_priv(vb->vb2_queue);
    release_mapping(s,as_buffer(vb));s->buffers[vb->index]=NULL;
}
static int queue_setup(struct vb2_queue *q,unsigned *n,unsigned *planes,unsigned sizes[],struct device *devices[])
{ if(*planes)return *planes==1 && sizes[0]>=SIZE?0:-EINVAL;*planes=1;sizes[0]=SIZE;if(*n<8)*n=8;if(*n>DUAL_MAX)*n=DUAL_MAX;return 0; }
static void queue_buffer(struct vb2_buffer *vb)
{
    struct capture *s=vb2_get_drv_priv(vb->vb2_queue);unsigned long f;
    spin_lock_irqsave(&s->lock,f);
    int ret=dual_queue(&s->core,vb->index);
    if(!ret && !s->core.stopping)for(unsigned i=0;i<2;i++)queue_work(s->workers,&s->ch[i].work);
    spin_unlock_irqrestore(&s->lock,f);
    if(ret)vb2_buffer_done(vb,VB2_BUF_STATE_ERROR);
}
static void return_all(struct capture *s,enum vb2_buffer_state state)
{
    for(unsigned i=0;i<s->core.count;i++)if(s->core.slots[i].state!=D_FREE && s->core.slots[i].state!=D_USER) {
        s->core.slots[i].state=D_USER;
        if(s->buffers[i])vb2_buffer_done(&s->buffers[i]->vb.vb2_buf,state);
    }
    s->core.ch[0].id=s->core.ch[1].id=-1;
}
static int start_stream(struct vb2_queue *q,unsigned count)
{
    struct capture *s=vb2_get_drv_priv(q);unsigned long f;
    if(s->fatal || s->disconnected)goto error;
    for(unsigned i=0;i<2;i++)if(cancel_channel(&s->ch[i]))goto error;
    u64 before=ktime_get_ns();s->tick_base=tick64(&s->ch[0],TICK_LO,TICK_HI);
    s->ns_base=before+(ktime_get_ns()-before)/2;
    spin_lock_irqsave(&s->lock,f);dual_start(&s->core);s->late=0;
    for(unsigned i=0;i<2;i++) {
        s->ch[i].timedout=false;s->ch[i].miss_start=readl(s->ch[i].gate+MISS);
        s->ch[i].miss_end=s->ch[i].miss_start;s->ch[i].frames=0;s->ch[i].errors=0;
        queue_work(s->workers,&s->ch[i].work);
    }
    spin_unlock_irqrestore(&s->lock,f);return 0;
error:return_all(s,VB2_BUF_STATE_QUEUED);return -EIO;
}
static void stop_stream(struct vb2_queue *q)
{
    struct capture *s=vb2_get_drv_priv(q);unsigned long f;
    spin_lock_irqsave(&s->lock,f);
    if(!s->core.stopping)for(unsigned i=0;i<2;i++)if(s->ch[i].dma)s->ch[i].miss_end=readl(s->ch[i].gate+MISS);
    dual_stop(&s->core);
    for(unsigned i=0;i<2;i++)if(s->ch[i].dma) {
        writel(0,s->ch[i].gate+IRQ_ENABLE);writel(ABORT,s->ch[i].gate+CONTROL);
    }
    spin_unlock_irqrestore(&s->lock,f);
    for(unsigned i=0;i<2;i++)cancel_work_sync(&s->ch[i].work);
    for(unsigned i=0;i<2;i++)cancel_delayed_work_sync(&s->ch[i].timeout);
    cancel_delayed_work_sync(&s->publish);
    for(unsigned i=0;i<2;i++)if(s->ch[i].dma) {
        if(!cancel_channel(&s->ch[i])){kfree(s->ch[i].ctx);s->ch[i].ctx=NULL;}
        dev_info(s->v4l2.dev,"ch%u frames=%llu errors=%llu miss_delta=%u\n",i,s->ch[i].frames,s->ch[i].errors,
            (u32)(s->ch[i].miss_end-s->ch[i].miss_start));
    }
    return_all(s,VB2_BUF_STATE_ERROR);
    dev_info(s->v4l2.dev,"late_frames=%llu stopped_generation=%u\n",s->late,s->core.generation);
}
static const struct vb2_ops qops={.queue_setup=queue_setup,.buf_prepare=prepare_buffer,.buf_cleanup=cleanup_buffer,
    .buf_queue=queue_buffer,.start_streaming=start_stream,.stop_streaming=stop_stream,
    .wait_prepare=vb2_ops_wait_prepare,.wait_finish=vb2_ops_wait_finish};
static void format(struct v4l2_format *f)
{ f->fmt.pix=(struct v4l2_pix_format){.width=W,.height=H,.pixelformat=V4L2_PIX_FMT_YUYV,.field=V4L2_FIELD_NONE,
    .bytesperline=STRIDE,.sizeimage=SIZE,.colorspace=V4L2_COLORSPACE_REC709,.ycbcr_enc=V4L2_YCBCR_ENC_709,
    .quantization=V4L2_QUANTIZATION_LIM_RANGE,.xfer_func=V4L2_XFER_FUNC_709}; }
static int querycap(struct file *file,void *fh,struct v4l2_capability *c)
{ strscpy(c->driver,"sc130_capture_v2",sizeof(c->driver));strscpy(c->card,"SC130 dual YUY2",sizeof(c->card));strscpy(c->bus_info,"platform:sc130-dual",sizeof(c->bus_info));return 0; }
static int enum_fmt(struct file *file,void *fh,struct v4l2_fmtdesc *f)
{ if(f->index)return -EINVAL;f->pixelformat=V4L2_PIX_FMT_YUYV;return 0; }
static int get_fmt(struct file *file,void *fh,struct v4l2_format *f) { format(f);return 0; }
static int set_fmt(struct file *file,void *fh,struct v4l2_format *f)
{ struct capture *s=video_drvdata(file);if(vb2_is_busy(&s->queue))return -EBUSY;format(f);return 0; }
static int sizes(struct file *file,void *fh,struct v4l2_frmsizeenum *f)
{ if(f->index || f->pixel_format!=V4L2_PIX_FMT_YUYV)return -EINVAL;f->type=V4L2_FRMSIZE_TYPE_DISCRETE;f->discrete.width=W;f->discrete.height=H;return 0; }
static const struct v4l2_ioctl_ops iops={.vidioc_querycap=querycap,.vidioc_enum_fmt_vid_cap=enum_fmt,
    .vidioc_g_fmt_vid_cap=get_fmt,.vidioc_try_fmt_vid_cap=get_fmt,.vidioc_s_fmt_vid_cap=set_fmt,.vidioc_enum_framesizes=sizes,
    .vidioc_reqbufs=vb2_ioctl_reqbufs,.vidioc_create_bufs=vb2_ioctl_create_bufs,.vidioc_prepare_buf=vb2_ioctl_prepare_buf,
    .vidioc_querybuf=vb2_ioctl_querybuf,.vidioc_qbuf=vb2_ioctl_qbuf,.vidioc_dqbuf=vb2_ioctl_dqbuf,.vidioc_expbuf=vb2_ioctl_expbuf,
    .vidioc_streamon=vb2_ioctl_streamon,.vidioc_streamoff=vb2_ioctl_streamoff};
static const struct v4l2_file_operations fops={.owner=THIS_MODULE,.open=v4l2_fh_open,.release=vb2_fop_release,
    .poll=vb2_fop_poll,.mmap=vb2_fop_mmap,.unlocked_ioctl=video_ioctl2};
static void video_release(struct video_device *v)
{
    struct capture *s=container_of(v,struct capture,video);
    /* A fatal unresponsive DMA may still invoke its retained callback context. */
    if(s->fatal)return;
    mutex_destroy(&s->mutex);kfree(s);
}
static int probe(struct platform_device *pdev)
{
    struct device *dev=&pdev->dev;int ret;u32 bits;
    struct capture *s=kzalloc(sizeof(*s),GFP_KERNEL);if(!s)return -ENOMEM;
    mutex_init(&s->mutex);spin_lock_init(&s->lock);dual_init(&s->core,DUAL_MAX);
    INIT_DELAYED_WORK(&s->publish,publish_work);
    s->workers=alloc_workqueue("sc130-v2",WQ_HIGHPRI|WQ_MEM_RECLAIM,4);
    if(!s->workers){ret=-ENOMEM;goto fail;}
    ret=of_property_read_u32(dev->of_node,"dma-address-bits",&bits);
    if(ret || bits!=32){ret=-EINVAL;goto fail;}
    for(unsigned i=0;i<2;i++) {
        struct channel *ch=&s->ch[i];char name[16];ch->s=s;ch->index=i;ch->irq=-1;
        INIT_WORK(&ch->work,channel_work);INIT_DELAYED_WORK(&ch->timeout,timeout_work);
        snprintf(name,sizeof(name),"gate%u",i);ch->gate=devm_platform_ioremap_resource_byname(pdev,name);
        if(IS_ERR(ch->gate)){ret=PTR_ERR(ch->gate);ch->gate=NULL;goto fail;}
        if(!dual_identity(i,readl(ch->gate+ID),readl(ch->gate+VERSION),readl(ch->gate+CAP),readl(ch->gate+PHASE))) {
            ret=-ENODEV;goto fail;
        }
        writel(0,ch->gate+IRQ_ENABLE);
        snprintf(name,sizeof(name),"capture%u",i);ch->dma=dma_request_chan(dev,name);
        if(IS_ERR(ch->dma)){ret=PTR_ERR(ch->dma);ch->dma=NULL;goto fail;}
        if(i && ch->dma->device==s->ch[0].dma->device){ret=-EINVAL;goto fail;}
        if(dma_get_mask(ch->dma->device->dev)>DMA_BIT_MASK(32)){ret=-ERANGE;goto fail;}
        snprintf(name,sizeof(name),"frame%u",i);ret=platform_get_irq_byname(pdev,name);if(ret<0)goto fail;
        int irq=ret;ret=request_irq(irq,gate_irq,0,dev_name(dev),ch);if(ret)goto fail;ch->irq=irq;
    }
    s->queue=(struct vb2_queue){.type=V4L2_BUF_TYPE_VIDEO_CAPTURE,.io_modes=VB2_MMAP,.drv_priv=s,
        .buf_struct_size=sizeof(struct buffer),.ops=&qops,.mem_ops=&vb2_dma_contig_memops,
        .timestamp_flags=V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC|V4L2_BUF_FLAG_TSTAMP_SRC_EOF,
        /* Kernel 6.12 requires max_num_buffers >= VB2_MAX_FRAME. queue_setup
         * separately caps the actual allocation to our DUAL_MAX slots. */
        .lock=&s->mutex,.dev=s->ch[0].dma->device->dev,.min_queued_buffers=8,.allow_cache_hints=1};
    ret=vb2_queue_init(&s->queue);if(ret)goto fail;
    ret=v4l2_device_register(dev,&s->v4l2);if(ret)goto fail;
    strscpy(s->video.name,"SC130 dual YUY2",sizeof(s->video.name));s->video.v4l2_dev=&s->v4l2;
    s->video.fops=&fops;s->video.ioctl_ops=&iops;s->video.lock=&s->mutex;s->video.queue=&s->queue;
    s->video.release=video_release;s->video.device_caps=V4L2_CAP_VIDEO_CAPTURE|V4L2_CAP_STREAMING;
    video_set_drvdata(&s->video,s);platform_set_drvdata(pdev,s);
    ret=video_register_device(&s->video,VFL_TYPE_VIDEO,-1);if(!ret)return 0;
    v4l2_device_unregister(&s->v4l2);
fail:
    for(unsigned i=0;i<2;i++) {
        if(s->ch[i].irq>=0 && s->ch[i].s)free_irq(s->ch[i].irq,&s->ch[i]);
        if(s->ch[i].dma)dma_release_channel(s->ch[i].dma);
    }
    if(s->workers)destroy_workqueue(s->workers);
    mutex_destroy(&s->mutex);kfree(s);return ret;
}
static void remove_device(struct platform_device *pdev)
{
    struct capture *s=platform_get_drvdata(pdev);
    mutex_lock(&s->mutex);s->disconnected=true;vb2_queue_error(&s->queue);stop_stream(&s->queue);
    for(unsigned i=0;i<2;i++){free_irq(s->ch[i].irq,&s->ch[i]);dma_release_channel(s->ch[i].dma);s->ch[i].dma=NULL;}
    destroy_workqueue(s->workers);s->workers=NULL;v4l2_device_disconnect(&s->v4l2);
    mutex_unlock(&s->mutex);v4l2_device_unregister(&s->v4l2);video_unregister_device(&s->video);
}
static const struct of_device_id matches[]={ {.compatible="sc130,dual-frame-gate-v2"},{} };
MODULE_DEVICE_TABLE(of,matches);
static struct platform_driver sc130_v2_driver={.probe=probe,.remove=remove_device,.driver={.name="sc130_capture_v2",.of_match_table=matches}};
module_platform_driver(sc130_v2_driver);
MODULE_DESCRIPTION("Experimental SC130 dual physical VDMA capture");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(DMA_BUF);
