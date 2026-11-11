/*
 * GOATTRACKER command queue (menus, toolbar, drag & drop)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gcommand.h"

#define CMDQUEUESIZE 32
#define CMDPATHSIZE 1024

typedef struct
{
  int cmd;
  char *path;
} QUEUEDCMD;

static QUEUEDCMD cmdqueue[CMDQUEUESIZE];
static int cmdhead = 0;
static int cmdtail = 0;

void cmd_postfile(int cmd, const char *path)
{
  int next = (cmdtail + 1) % CMDQUEUESIZE;

  if (next == cmdhead) return; // Full
  cmdqueue[cmdtail].cmd = cmd;
  cmdqueue[cmdtail].path = NULL;
  if ((path) && (path[0]))
  {
    size_t len = strlen(path);
    cmdqueue[cmdtail].path = (char *)malloc(len + 1);
    if (cmdqueue[cmdtail].path) memcpy(cmdqueue[cmdtail].path, path, len + 1);
  }
  cmdtail = next;
}

void cmd_post(int cmd)
{
  cmd_postfile(cmd, NULL);
}

int cmd_poll(int *cmd, char *path, int pathsize)
{
  if (cmdhead == cmdtail) return 0;

  *cmd = cmdqueue[cmdhead].cmd;
  if ((path) && (pathsize > 0))
  {
    path[0] = 0;
    if (cmdqueue[cmdhead].path)
    {
      strncpy(path, cmdqueue[cmdhead].path, pathsize - 1);
      path[pathsize - 1] = 0;
    }
  }
  free(cmdqueue[cmdhead].path);
  cmdqueue[cmdhead].path = NULL;
  cmdhead = (cmdhead + 1) % CMDQUEUESIZE;
  return 1;
}

int cmd_pending(void)
{
  return cmdhead != cmdtail;
}
