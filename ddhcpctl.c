#include <arpa/inet.h>
#include <stdlib.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>

#include "tools.h"

int main(int argc, char** argv) {

  int c;
  int ctl_sock;
  int show_usage = 0;
  unsigned int msglen = 0;
  dhcp_option* option = NULL;

  if (argc == 1) {
    show_usage = 1;
  }

  char* path = "/tmp/ddhcpd_ctl";

#define BUFSIZE_MAX 1500
  uint8_t* buffer = (uint8_t*) calloc(sizeof(uint8_t), BUFSIZE_MAX);

  while ((c = getopt(argc, argv, "C:t:bdho:r:")) != -1) {
    switch (c) {
    case 'h':
      show_usage = 1;
      break;

    case 'b':
      //show blocks
      msglen = 1;
      buffer[0] = (char) 1;
      break;

    case 'd':
      // show dhcp
      msglen = 1;
      buffer[0] = (char) 2;
      break;

    case 'o':
      option = parse_option();
      break;

    case 'r':
      msglen = 1;
      buffer[0] = (char) 4;
      buffer[1] = (char) atoi(optarg);
      break;

    case 'C':
      path = optarg;
      break;

    default:
      printf("ARGC: %i\n", argc);
      show_usage = 1;
      break;
    }
  }

  // Check if a dhcp option code should be set and if all parameters
  // for that are given.
  if (option != NULL) {
    msglen = 3 + option->len;
    buffer[0] = (char) 3;
    buffer[1] = (char) option->code;
    buffer[2] = (char) option->len;
    memcpy(buffer + 3, option->payload, sizeof(option));
    free(option);
  }

  if (show_usage) {
    printf("Usage: ddhcpctl [-h|-b|-d|-o <option>|-C PATH]\n");
    printf("\n");
    printf("-h                   This usage information.\n");
    printf("-b                   Show current block usage.\n");
    printf("-d                   Show the current dhcp options store.\n");
    printf("-o CODE;LEN;P1,..,Pn Set DHCP Option with code,len and #len chars in decimal\n");
    printf("-r CODE              Remove DHCP Option");
    printf("-C PATH              Path to control socket\n");
    exit(0);
  }

  struct sockaddr_un s_un;

  if ((ctl_sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0) {
    perror("can't create socket:");
    return (-1);
  }

  memset(&s_un, 0, sizeof(struct sockaddr_un));
  s_un.sun_family = AF_UNIX;


  strncpy(s_un.sun_path, path, sizeof(s_un.sun_path));

  if (connect(ctl_sock, (struct sockaddr*)&s_un, sizeof(s_un)) < 0) {
    perror("can't connect to control socket");
    free(buffer);
    close(ctl_sock);
    return -1;
  }

  int ret;
  ret = fcntl(ctl_sock, F_GETFL, 0);

  if (ret < 0) {
    perror("Cant't set stuff:");
  }

  size_t bw = send(ctl_sock, buffer, msglen, 0);

  if (bw < msglen) {
    printf("Wrote %i / %i bytes to control socket", (int) bw, msglen);
    perror("send error:");
    return -1;
  }

  size_t br = 0;

  while ((br = recv(ctl_sock, (char*) buffer, sizeof(buffer), 0))) {
    buffer[br] = '\0';
    printf("%s", (char*) buffer);
  }

  close(ctl_sock);
  free(buffer);
}
