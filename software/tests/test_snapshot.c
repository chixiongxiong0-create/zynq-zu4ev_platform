#define main platform_bridge_main
#include "../platform_bridge.c"
#undef main
#include <assert.h>
int main(void)
{
    snapshot_requested=0;snapshot_signal(SIGUSR1);assert(snapshot_requested==1);
    struct platform p={0};broker_init(&p.broker,8);
    assert(snapshot_latest(&p,"/tmp/unused-snapshot.yuy2")==-ENODATA);
    assert(broker_dequeue(&p.broker,0)==0);assert(broker_latest(&p.broker,0)==0);
    unsigned char *frame=malloc(SC130_SIZE);assert(frame);memset(frame,0x80,SC130_SIZE);
    p.cap.b[0].ptr=frame;p.cap.b[0].length=SC130_SIZE;p.meta[0].sequence=42;
    p.meta[0].bytesused=SC130_SIZE-1;
    assert(snapshot_latest(&p,"/tmp/unused-snapshot.yuy2")==-EINVAL);
    p.meta[0].bytesused=SC130_SIZE;
    assert(snapshot_latest(&p,"/no-such-sc130-directory/frame.yuy2")==-ENOENT);
    assert(p.broker.latest==0 && !broker_can_queue(&p.broker,0));
    char dir[]="/tmp/sc130-snapshot-test-XXXXXX";assert(mkdtemp(dir));
    char path[256];snprintf(path,sizeof(path),"%s/frame.yuy2",dir);
    assert(snapshot_latest(&p,path)==0);
    struct stat info;assert(stat(path,&info)==0 && info.st_size==SC130_SIZE);
    FILE *in=fopen(path,"rb");assert(in);assert(fgetc(in)==0x80);
    assert(fseek(in,SC130_SIZE-1,SEEK_SET)==0 && fgetc(in)==0x80);assert(fclose(in)==0);
    assert(p.broker.latest==0 && !p.broker.queued[0] && !broker_can_queue(&p.broker,0));
    unlink(path);rmdir(dir);free(frame);
    puts("snapshot signal, size, IO failure and retained ownership tests passed");
    return 0;
}
