#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "NuppelVideoRecorder.h"
#include "NuppelVideoPlayer.h"
#include "RingBuffer.h"
#include "XJ.h"
#include "settings.h"

#include "channel.h"

void *SpawnEncode(void *param)
{
    NuppelVideoRecorder *nvr = (NuppelVideoRecorder *)param;

    nvr->StartRecording();
  
    return NULL;
}

void *SpawnDecode(void *param)
{
    NuppelVideoPlayer *nvp = (NuppelVideoPlayer *)param;

    nvp->StartPlaying();

    return NULL;
}

int main(int argc, char *argv[])
{
  Settings settings("settings.txt");

  Channel *channel = new Channel("/dev/video");

  channel->Open();

  channel->SetFormat(settings.GetSetting("TVFormat"));
  channel->SetFreqTable(settings.GetSetting("FreqTable"));

  channel->SetChannelByString("3");

  channel->Close();

  pthread_t encode, decode; 
  
  long long filesize = settings.GetNumSetting("BufferSize");
  filesize = filesize * 1024 * 1024 * 1024;
  long long smudge = settings.GetNumSetting("MaxBufferFill");
  smudge = smudge * 1024 * 1024; 

  int osd_display_time = settings.GetNumSetting("OSDDisplayTime");
  
  RingBuffer *rbuffer = new RingBuffer(settings.GetSetting("BufferName"), 
		                       filesize, smudge);
  
  NuppelVideoRecorder *nvr = new NuppelVideoRecorder();
  nvr->SetRingBuffer(rbuffer);
  nvr->SetMotionLevels(settings.GetNumSetting("LumaFilter"), 
                       settings.GetNumSetting("ChromaFilter"));
  nvr->SetQuality(settings.GetNumSetting("Quality"));
  nvr->SetResolution(settings.GetNumSetting("Width"),
                     settings.GetNumSetting("Height"));
  nvr->SetMP3Quality(settings.GetNumSetting("MP3Quality"));
  nvr->Initialize();
 
  NuppelVideoPlayer *nvp = new NuppelVideoPlayer();
  nvp->SetRingBuffer(rbuffer);
  nvp->SetRecorder(nvr);
  nvp->SetDeinterlace((bool)settings.GetNumSetting("Deinterlace"));
  
  pthread_create(&encode, NULL, SpawnEncode, nvr);

  while (!nvr->IsRecording())
      usleep(50);

  channel->SetFd(nvr->GetVideoFd());

  sleep(1);
  pthread_create(&decode, NULL, SpawnDecode, nvp);

  while (!nvp->IsPlaying())
      usleep(50);

  int keypressed;
  bool paused = false;
  float frameRate = nvp->GetFrameRate();
  
  cout << endl;

  while (nvp->IsPlaying())
  {
      usleep(50);
      if ((keypressed = XJ_CheckEvents()))
      {
           switch (keypressed) {
	       case 'p':
               case 'P': paused = nvp->TogglePause(); break;
               case wsRight: nvp->FastForward(5); break;
               case wsLeft: nvp->Rewind(5); break;
               case wsEscape: nvp->StopPlaying(); break;
               case wsUp: {
                            nvp->Pause();
			    while (!nvp->GetPause()) 
                                   usleep(5);

                            nvr->Pause();
                            while (!nvr->GetPause())
                                 usleep(5);
				 
                            rbuffer->Reset();
			    channel->ChannelUp();

			    nvr->Reset();
			    nvr->Unpause();
			    
			    usleep(500000);
			    nvp->ResetPlaying();
			    while (!nvp->ResetYet())
                                usleep(5);
			    nvp->SetInfoText("Placeholder", osd_display_time);
			    nvp->Unpause();
                          } break;
               case wsDown: {
                            nvp->Pause();
                            while (!nvp->GetPause())
                                   usleep(5);

                            nvr->Pause();
                            while (!nvr->GetPause())
                                 usleep(5);

                            rbuffer->Reset();

                            channel->ChannelDown();

                            nvr->Reset();
                            nvr->Unpause();

                            usleep(500000);
                            nvp->ResetPlaying();
                            while (!nvp->ResetYet())
                                usleep(5);
			    nvp->SetInfoText("Placeholder 2", osd_display_time);
                            nvp->Unpause();
                          } break;
               default: break;
           }
      }
      if (paused)
      {
          fprintf(stderr, "\r Paused: %f seconds behind realtime (%f%% buffer left)", (float)(nvr->GetFramesWritten() - nvp->GetFramesPlayed()) / frameRate, (float)rbuffer->GetFreeSpace() / (float)rbuffer->GetFileSize() * 100.0);
      }
      else
      {
	  fprintf(stderr, "\r                                                                      ");
          fprintf(stderr, "\r Playing: %f seconds behind realtime (%lld skipped frames)", (float)(nvr->GetFramesWritten() - nvp->GetFramesPlayed()) / frameRate, nvp->GetFramesSkipped());
      }
  }

  printf("exited for some reason\n");
  
  nvr->StopRecording();
  
  pthread_join(encode, NULL);
  pthread_join(decode, NULL);

  delete nvr;
  delete nvp;
  return 0;
}
