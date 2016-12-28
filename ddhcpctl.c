#include <arpa/inet.h>
#include <stdlib.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {

  int c;
  int ctl_sock;
  int show_usage = 0;
  uint8_t* buffer = (uint8_t*) calloc(sizeof(uint8_t), 1500);
  unsigned int msglen = 0;
  uint8_t dhcp_option_code = 0;
  uint8_t dhcp_option_len = 0;
  uint8_t *dhcp_option_payload = NULL;
  uint8_t dhcp_option_payload_counter = 0;

  if ( argc == 1 ) show_usage = 1;

  while (( c = getopt(argc,argv,"t:bdhc:l:p:")) != -1 ) {
    switch(c) {
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
    case 'c':
      // dhcp option code
      dhcp_option_code = atoi(optarg);
      break;
    case 'l':
      // dhpc option len
      if ( dhcp_option_code > 0) {
        dhcp_option_len = atoi(optarg);
        printf("DHCP_OPTION_LEN: %i\n",dhcp_option_len);
        dhcp_option_payload = (uint8_t*) calloc(dhcp_option_len,sizeof(uint8_t));
      } else {
        printf("No DHCP Code given");
        show_usage = 1;
      }
      break;
    case 'p':
      // dhcp option payload
      if ( dhcp_option_len > dhcp_option_payload_counter ) {
        printf("DHCP_OPTION_PAYLOAD[%i]: %i\n",dhcp_option_payload_counter,atoi(optarg));
        dhcp_option_payload[dhcp_option_payload_counter++] = atoi(optarg);
      } else {
        printf("Payload len exceeded or no dhcp option len set.\n");
        show_usage = 1;
      }
      break;
    default:
      printf("ARGC: %i\n",argc);
      show_usage = 1;
      break;
    }
  }

  // Check if a dhcp option code should be set and if all parameters
  // for that are given.
  if ( dhcp_option_code > 0 ) {
    if ( dhcp_option_len > 0 ) {
      if ( dhcp_option_payload_counter == dhcp_option_len ) {
        msglen = 3 + dhcp_option_len;
        buffer[0] = (char) 3;
        buffer[1] = (char) dhcp_option_code;
        buffer[2] = (char) dhcp_option_len;
        for ( uint8_t i = 0; i < dhcp_option_len;i++){
          buffer[3 + i] = dhcp_option_payload[i];
        }
      } else {
        printf("Not enough DHCP payload given.\n");
        free(dhcp_option_payload);
        show_usage = 1;
      }
    } else {
      printf("No DHCP option len given.\n");
      show_usage = 1;
    }
  }


  if(show_usage) {
    printf("Usage: ddhcpctl [-h|-b|-d|-c <num> -l <num> (-p <num>)+]\n");
    printf("\n");
    printf("-h              This usage information.\n");
    printf("-b              Show current block usage.\n");
    printf("-d              Show the current dhcp options store.\n");
    printf("-c <num>        Set a dhcp option code.\n");
    printf("-l <num>        Set the dhco option len.\n");
    printf("-p <num>        Set the payload of a dhcp option decoded as integer.\n");
    exit (0);
  }

  struct sockaddr_un s_un;

  if ((ctl_sock = socket( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0) {
    perror("can't create socket:");
    return (-1);
  }

  memset(&s_un, 0, sizeof(struct sockaddr_un));
  s_un.sun_family = AF_UNIX;

  char *path = "/tmp/ddhcpd_ctl";

  strncpy(s_un.sun_path, path , sizeof(s_un.sun_path));
  if(connect(ctl_sock, (struct sockaddr*)&s_un, sizeof(s_un)) < 0) {
    perror("can't connect to control socket");
    free(buffer);
    close(ctl_sock);
    return -1;
  }
  int ret;
  ret = fcntl(ctl_sock, F_GETFL, 0);
  if ( ret < 0 ) {
    perror("Cant't set stuff:");
  }
  size_t bw = send(ctl_sock,buffer,msglen,0);
  if( bw < msglen ) {
    printf("Wrote %i / %i bytes to control socket",(int) bw, msglen);
    perror("send error:");
    return -1;
  }
  size_t br = 0;
  while((br = recv(ctl_sock, (char *) buffer,sizeof(buffer),0))){
    buffer[br] = '\0';
    printf("%s",(char *) buffer);
  }

  close(ctl_sock);
  free(buffer);
  if ( dhcp_option_payload != NULL ) free(dhcp_option_payload);
}
