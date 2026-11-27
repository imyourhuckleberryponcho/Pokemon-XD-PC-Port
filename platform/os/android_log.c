#ifdef __ANDROID__
#include <android/log.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int sPipe[2];

static void* reader(void* unused) {
  char buf[4096];
  size_t have = 0;
  (void)unused;
  for (;;) {
    const ssize_t got = read(sPipe[0], buf + have, sizeof(buf) - 1 - have);
    char* line = buf;
    char* end;
    if (got <= 0) {
      if (got < 0 && have == 0) continue;
      break;
    }
    have += (size_t)got;
    buf[have] = 0;
    while ((end = memchr(line, '\n', have - (size_t)(line - buf))) != NULL) {
      *end = 0;
      __android_log_write(ANDROID_LOG_INFO, "XD", line);
      line = end + 1;
    }
    have -= (size_t)(line - buf);
    memmove(buf, line, have);
    if (have >= sizeof(buf) - 1) {
      buf[have] = 0;
      __android_log_write(ANDROID_LOG_INFO, "XD", buf);
      have = 0;
    }
  }
  return NULL;
}

void PortAndroid_StdioToLogcat(void) {
  pthread_t t;
  static int done;
  if (done) return;
  done = 1;
  if (pipe(sPipe) != 0) return;
  setvbuf(stdout, NULL, _IOLBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  dup2(sPipe[1], 1);
  dup2(sPipe[1], 2);
  if (pthread_create(&t, NULL, reader, NULL) == 0) pthread_detach(t);
}
#endif
