/**
 *		Example by Nba_Yoh base on the one of smealum, thanks to him.
 */

#include <string.h>
#include <malloc.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/time.h>

#include <3ds.h>

Handle threadHandle, threadExitRequest, y2rEvent;

#define STACKSIZE (4 * 1024)

volatile bool threadExit = false;
volatile u8 *send, *recv;

#define WAIT_TIMEOUT 1000000000ULL
#define WIDTH 400
#define HEIGHT 240
#define SCREEN_SIZE WIDTH * HEIGHT * 2
#define BUF_SIZE SCREEN_SIZE * 2

void writePictureToFramebufferRGB24_Y2R(void *fb, void *img, u16 x, u16 y, u16 width, u16 height) {
    u8 *fb_8 = (u8*) fb;
    u8 *img_8 = (u8*) img;

    fb_8+= HEIGHT*3 - 8*3;
    for(int j = 0; j < HEIGHT/8; j++, fb_8-=HEIGHT*WIDTH*3+8*3)
        for(int i = 0; i < WIDTH; i++, img_8+=3*8, fb_8+=HEIGHT*3)
            memcpy(fb_8, img_8, 1*8*3);

}

void cameraThread(void *arg)
{
    acInit();

    gfxSetDoubleBuffering(GFX_TOP, true);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);

    camInit();

    printf("CAMU_SetSize: 0x%08X\n", (unsigned int) CAMU_SetSize(SELECT_OUT1_OUT2, SIZE_CTR_TOP_LCD, CONTEXT_A));
    printf("CAMU_SetOutputFormat: 0x%08X\n", (unsigned int) CAMU_SetOutputFormat(SELECT_OUT1_OUT2, OUTPUT_YUV_422, CONTEXT_A));

    printf("CAMU_SetFrameRate: 0x%08X\n", (unsigned int) CAMU_SetFrameRate(SELECT_OUT1_OUT2, FRAME_RATE_30));

    printf("CAMU_SetNoiseFilter: 0x%08X\n", (unsigned int) CAMU_SetNoiseFilter(SELECT_OUT1_OUT2, true));
    printf("CAMU_SetAutoExposure: 0x%08X\n", (unsigned int) CAMU_SetAutoExposure(SELECT_OUT1_OUT2, true));
    printf("CAMU_SetAutoWhiteBalance: 0x%08X\n", (unsigned int) CAMU_SetAutoWhiteBalance(SELECT_OUT1_OUT2, true));

    printf("CAMU_SetTrimming: 0x%08X\n", (unsigned int) CAMU_SetTrimming(PORT_CAM1, false));
    printf("CAMU_SetTrimming: 0x%08X\n", (unsigned int) CAMU_SetTrimming(PORT_CAM2, false));

    if(!send) {
        printf("Failed to allocate memory!");
        svcExitThread();
    }

    u32 bufSize;
    printf("CAMU_GetMaxBytes: 0x%08X\n", (unsigned int) CAMU_GetMaxBytes(&bufSize, WIDTH, HEIGHT));
    printf("CAMU_SetTransferBytes: 0x%08X\n", (unsigned int) CAMU_SetTransferBytes(PORT_BOTH, bufSize, WIDTH, HEIGHT));

    printf("CAMU_Activate: 0x%08X\n", (unsigned int) CAMU_Activate(SELECT_OUT1_OUT2));

    Handle camReceiveEvent = 0;
    Handle camReceiveEvent2 = 0;

    printf("CAMU_ClearBuffer: 0x%08X\n", (unsigned int) CAMU_ClearBuffer(PORT_BOTH));
    printf("CAMU_SynchronizeVsyncTiming: 0x%08X\n", (unsigned int) CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2));

    printf("CAMU_StartCapture: 0x%08X\n", (unsigned int) CAMU_StartCapture(PORT_BOTH));
    printf("CAMU_PlayShutterSound: 0x%08X\n", (unsigned int) CAMU_PlayShutterSound(SHUTTER_SOUND_TYPE_MOVIE));

    while(!threadExit)
    {

        CAMU_SetReceiving(&camReceiveEvent, (void*) send, PORT_CAM1, SCREEN_SIZE, (s16) bufSize);
        CAMU_SetReceiving(&camReceiveEvent2, (void*)(send + SCREEN_SIZE), PORT_CAM2, SCREEN_SIZE, (s16) bufSize);

        svcWaitSynchronization(camReceiveEvent, WAIT_TIMEOUT);
        svcWaitSynchronization(camReceiveEvent2, WAIT_TIMEOUT);

        svcCloseHandle(camReceiveEvent);
        svcCloseHandle(camReceiveEvent2);
    }

    printf("CAMU_StopCapture: 0x%08X\n", (unsigned int) CAMU_StopCapture(PORT_BOTH));

    printf("CAMU_Activate: 0x%08X\n", (unsigned int) CAMU_Activate(SELECT_NONE));

    free((void*) send);
    camExit();
    acExit();
    svcSignalEvent(threadExitRequest);
    svcExitThread();
}

int main(int argc, char** argv)
{
    gfxInitDefault();

    consoleInit(GFX_BOTTOM, NULL);

    y2rInit();
    Y2R_ConversionParams convParams = {INPUT_YUV422_BATCH, OUTPUT_RGB_24 , ROTATION_CLOCKWISE_90,
                                       BLOCK_LINE, WIDTH, HEIGHT, COEFFICIENT_ITU_R_BT_709, 0, 0}; // output rotated and converted to RGB 24
    printf("Y2R_Params: 0x%08X\n", Y2RU_SetConversionParams(&convParams));


    svcCreateEvent(&threadExitRequest, 0);
    u32 *threadStack = (u32*)memalign(32, STACKSIZE);
    Result ret = svcCreateThread(&threadHandle, cameraThread, 0, &threadStack[STACKSIZE / 4], 0x2F, 0);

    printf("Thread create returned %x\n", ret);

    send = (u8*)malloc(BUF_SIZE);               // the buffer for the camera
    recv = (u8*)malloc((SCREEN_SIZE / 2) * 3);  // the necessary space for the rgb 24 datas of the image

    struct timeval previous;
    gettimeofday(&previous, 0);
    struct timeval now;
    int count = 0;

    u8 *fb;

    Y2RU_SetTransferEndInterrupt(true);
    Y2RU_GetTransferEndEvent(&y2rEvent);    // to activate the event trigger

    // Main loop
    while (aptMainLoop())
    {
        hidScanInput();

        u32 kDown = hidKeysDown();
        if(kDown & KEY_START)
            break; // break in order to return to hbmenu

        fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL); // get the current framebuffer

        //TODO: Explain the rotation
        Y2RU_SetSendingYUYV((u8 *) & send[0], WIDTH * 2 * HEIGHT, WIDTH * 2, 0);    // to send the YUV datas of the camera
        Y2RU_SetReceiving((void *) &recv[0], WIDTH * 3 * HEIGHT, 3 * WIDTH, 0);     // to receive the new rgb 24 datas
        Y2RU_StartConversion();                                                     // to start the conversion
        svcWaitSynchronization(y2rEvent, 1000 * 1000 * 10);                         // wait the end of the conversion

        writePictureToFramebufferRGB24_Y2R(fb, (void*) recv, 0, 0, WIDTH, HEIGHT);  // draw to the framebuffer

        gettimeofday(&now, 0);
        count++;

        if((int)((now.tv_sec)-(previous.tv_sec) > 0))
        {
            printf("%d\n", count);
            count = 0;
            previous = now;

        }

        // Flush and swap framebuffers
        gfxFlushBuffers();
        gspWaitForVBlank();
        gfxSwapBuffers();
    }

    threadExit = true; // tell thread to exit

    svcWaitSynchronization(threadExitRequest, WAIT_TIMEOUT); // wait the threadExit event

    svcCloseHandle(threadExitRequest);
    svcCloseHandle(threadHandle);
    free(threadStack);
    free((void*) recv);

    y2rExit();
    gfxExit();

    return 0;
}

