#include <stdio.h>
#include <string.h>
#include <joker_tv.h>

int main()
{
  struct joker_t joker;
  int ret = 0;

  memset(&joker, 0, sizeof(joker));
  /* open Joker TV on USB bus */
  if ((ret = joker_open(&joker)))
    return ret;
  printf("allocated joker\n");
}
