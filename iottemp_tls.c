

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <mraa/aio.h>
#include <mraa/gpio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

// global variables to use
char buffer[1024];

const int B = 4275;
const int pinTempSensor = 1;

mraa_gpio_context button;
mraa_aio_context  temp_sensor;

// global flags and file
int keepgoing    = 1;
int notPause     = 1;
int period       = 1;
FILE *logfile    = NULL;
int fahrenheit   = 1;
char *id         = NULL;
char *host       = NULL;
int port         = -1;
SSL *ssl         = NULL;
SSL_CTX *ssl_ctx = NULL;


// options
static struct option long_options[] = {{"log",    required_argument, 0, 'l'},
				       {"period", required_argument, 0, 'p'},
				       {"scale",  required_argument, 0, 's'},
				       {"id",     required_argument, 0, 'i'},
				       {"host",   required_argument, 0, 'h'}};


// personal error handler
void
errorHandle(int x, char *str, int exitNum)
{
  if      (x == 0)  fprintf(stderr, "Invalid Option: %s --log=FILENAME --period=n --scale=n --id=n --host=address portnum\n", str);
  else if (x == 1)  fprintf(stderr, "Invalid Period: %s\n",               str);
  else if (x == 2)  fprintf(stderr, "Invalid Scale: %s\n",                str);
  else if (x == 3)  fprintf(stderr, "Error With AIO Read: %s\n",          str);
  else if (x == 4)  fprintf(stderr, "Cannot Get Local Time: %s\n",        str);
  else if (x == 5)  fprintf(stderr, "Unable To Open File: %s\n",          str);
  else if (x == 6)  fprintf(stderr, "Unable To Poll: %s\n",               str);
  else if (x == 7)  fprintf(stderr, "Error With fgets: %s\n",             str);
  else if (x == 8)  fprintf(stderr, "Invalid Command: %s\n",              str);
  else if (x == 9)  fprintf(stderr, "Cannot Log Again: %s\n",             str);
  else if (x == 10) fprintf(stderr, "ID Number has to be 9 digits: %s\n", str);
  else if (x == 11) fprintf(stderr, "Invalid Port Number: %s\n",          str);
  else if (x == 12) fprintf(stderr, "Connection Error: %s\n",             str);
  else if (x == 13) fprintf(stderr, "Socket Creation Error: %s\n",        str); 
  else if (x == 14) fprintf(stderr, "Socket Cannot Be Closed: %s\n",      str);
  else if (x == 15) fprintf(stderr, "Unable To Send Msg Correctly: %s\n", str);
  else if (x == 16) fprintf(stderr, "Failed To Init SSL: %s\n",           str);
  
  exit(exitNum);
}


// interrupt signal handler
void
sighandle(int sig)
{
  if (sig == SIGINT) keepgoing = 0;
}


// button rise edge handler
void
button_pressed()
{
  keepgoing = 0;
  notPause  = 0;
}


// a function responsible for printing time and temp
void
printNormal()
{
  ssize_t readSize, writeSize;
  time_t rawtime;
  struct tm *timeinfo;
 
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  if (timeinfo == NULL) errorHandle(4, strerror(errno), 2);

  long long int a = mraa_aio_read(temp_sensor);
  if (a == -1) errorHandle(3, strerror(errno), 2);

  float resistance = (float)(1023.0/a) - 1.0;
  resistance *= 100000;

  float temperature = 1.0/(log(resistance/100000)/B+1/298.15)-273.15;
  if (fahrenheit) temperature = temperature * 9 / 5 + 32;

  readSize = sprintf(buffer, "%02d:%02d:%02d %.1f\n", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, temperature);
  writeSize = SSL_write(ssl, buffer, readSize);
  if (readSize != writeSize) errorHandle(15, buffer, 2);

  if (logfile != NULL) {
    fprintf(logfile, "%02d:%02d:%02d %.1f\n", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, temperature);
    fflush(logfile);
  }
}


// a function responsible for printing time and shutdown msg
void
printShutdown()
{
  ssize_t readSize, writeSize;
  time_t rawtime;
  struct tm *timeinfo;

  time(&rawtime);
  timeinfo = localtime(&rawtime);
  if (timeinfo == NULL) errorHandle(4, strerror(errno), 2);

  readSize = sprintf(buffer, "%02d:%02d:%02d SHUTDOWN\n", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  writeSize = SSL_write(ssl, buffer, readSize);
  if (readSize != writeSize) errorHandle(15, buffer, 2);

  if (logfile != NULL) {
    fprintf(logfile, "%02d:%02d:%02d SHUTDOWN\n", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    fflush(logfile);
    fclose(logfile);
  }
}



// a function responsible for printing time
void
printTime(int fd)
{
  ssize_t readSize, writeSize;
  time_t rawtime;
  struct tm *timeinfo;

  time(&rawtime);
  timeinfo = localtime(&rawtime);
  if (timeinfo == NULL) errorHandle(4, strerror(errno), 2);

  readSize = sprintf(buffer, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  writeSize = write(fd, buffer, readSize);
  if (readSize != writeSize) errorHandle(15, buffer, 2);
  
  if (logfile != NULL) {
    fprintf(logfile, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    fflush(logfile);
  }
}


// function responsible for printing temperature
void
printTemp(int fd)
{
  ssize_t readSize, writeSize;
  long long int a = mraa_aio_read(temp_sensor);
  if (a == -1) errorHandle(3, strerror(errno), 2);
  
  float resistance = (float)(1023.0/a) - 1.0;
  resistance *= 100000;

  float temperature = 1.0/(log(resistance/100000)/B+1/298.15)-273.15;
  if (fahrenheit) temperature = temperature * 9 / 5 + 32;

  readSize = sprintf(buffer, " %.1f\n", temperature);
  writeSize = write(fd, buffer, readSize);
  if (readSize != writeSize) errorHandle(15, buffer, 2);
  
  if (logfile != NULL) {
    fprintf(logfile, " %.1f\n", temperature);
    fflush(logfile);
  }
}


// new function to parse received command through socket fd
void
newParseCommand(char *str, ssize_t length)
{
  if (length < 4) errorHandle(8, str, 2);

  int i = 0, j = 0;
  while (i < length) {
    if (str[i] != '\n') ++i;
    else {
      str[i] = '\0';

      // OFF Command
      if (str[j] == 'O') {
	if (str[j+1] != 'F' ||
	    str[j+2] != 'F') errorHandle(8, str+j, 2);
	else {
	  keepgoing = 0;
	  notPause  = 0;
	}
      }

      // LOG Command
      else if (str[j] == 'L') {
	if (str[j+1] != 'O' || str[j+2] != 'G' || str[j+3] != ' ')
	  errorHandle(8, str, 2);
      }

      // PERIOD Command
      else if (str[j] == 'P') {
	if (str[j+1] != 'E' ||
	    str[j+2] != 'R' ||
	    str[j+3] != 'I' ||
	    str[j+4] != 'O' ||
	    str[j+5] != 'D' ||
	    str[j+6] != '=') errorHandle(8, str, 2);

	period = atoi(&str[j+7]);
	if (period <= 0) errorHandle(8, str, 2);
      }

      // SCALE Command
      else if (str[j] == 'S' && str[j+1] == 'C') {
	if (str[j+2] != 'A' ||
	    str[j+3] != 'L' ||
	    str[j+4] != 'E' ||
	    str[j+5] != '=') errorHandle(8, str, 2);

	if      (str[6] == 'C') fahrenheit = 0;
	else if (str[6] == 'F') fahrenheit = 1;
	else errorHandle(8, str, 2);
      }

      // START Command
      else if (str[j] == 'S' && str[j+1] == 'T' && str[j+2] == 'A') {
	if (strcmp("START", str+j) == 0) notPause = 1;
	else errorHandle(8, str, 2);
      }

      // STOP Command
      else if (str[j] == 'S' && str[j+1] == 'T' && str[j+2] == 'O') {
	if (strcmp("STOP", str+j) != 0) errorHandle(8, str, 2);
	else notPause = 0;
      }

      j = i + 1;
      ++i;
    }

    // write to logfile if set
    if (logfile != NULL) {
      fprintf(logfile, "%s\n", str);
      fflush(logfile);
    }
  }
}


// function to parse received command
void
parseCommand(char *str, ssize_t length)
{
  // set the string to compare
  if (length < 4) errorHandle(8, str, 2);
  str[length-1] = '\0';
  
  // OFF Command
  if (str[0] == 'O') {
    if (strcmp("OFF", str) == 0) {
      keepgoing = 0;
      notPause  = 0;
    }
    else errorHandle(8, str, 2);
  }

  // LOG Command
  else if (str[0] == 'L') {
    if (length < 5)  errorHandle(8, str, 2);
    
    if (str[1] != 'O' || str[2] != 'G' || str[3] != ' ') 
      errorHandle(8, str, 2);
  }
  
  // PERIOD Command
  else if (str[0] == 'P') {
    if (length < 8) errorHandle(8, str, 2);

    if (str[1] != 'E' ||
	str[2] != 'R' ||
	str[3] != 'I' ||
	str[4] != 'O' ||
	str[5] != 'D' ||
	str[6] != '=') errorHandle(8, str, 2);
    
    period = atoi(&str[7]);
    if (period <= 0) errorHandle(8, str, 2);
  }

  // SCALE Command
  else if (str[0] == 'S' && str[1] == 'C') {
    if (length != 8) errorHandle(8, str, 2);

    if (str[2] != 'A' ||
	str[3] != 'L' ||
	str[4] != 'E' ||
	str[5] != '=') errorHandle(8, str, 2);

    if      (str[6] == 'C') fahrenheit = 0;
    else if (str[6] == 'F') fahrenheit = 1;
    else errorHandle(8, str, 2);
  }

  // START Command
  else if (str[0] == 'S' && str[1] == 'T' && str[2] == 'A') {
    if (length != 6) errorHandle(8, str, 2);
    else if (strcmp(str, "START") == 0) notPause = 1;
    else errorHandle(8, str, 2);
  }

  // STOP Command
  else if (str[0] == 'S' && str[1] == 'T' && str[2] == 'O') {
    if (strcmp("STOP", str) != 0) errorHandle(8, str, 2);
    else notPause = 0;
  }

  // Invalid Command
  else errorHandle(8, str, 2);

  // write to logfile if set
  if (logfile != NULL) {
    fprintf(logfile, "%s\n", str);
    fflush(logfile);
  }
}


// a function to set up connection on client side and return socket fd
int
client_connect()
{
  // object of server data
  struct sockaddr_in server_address;

  // create a new socket
  int socketfd = socket(AF_INET, SOCK_STREAM, 0);
  if (socketfd == -1) errorHandle(13, strerror(errno), 2);

  // lookup ip address by the host name
  struct hostent *server = gethostbyname(host);
  if (server == NULL) errorHandle(13, strerror(errno), 2);

  // copy over the ip address to the server data object
  memset(&server_address, 0, sizeof(struct sockaddr_in));
  server_address.sin_family = AF_INET;
  memcpy(&server_address.sin_addr.s_addr, server->h_addr, server->h_length);

  // convert the port to correct endian and apply
  server_address.sin_port = htons((unsigned int)port);

  // connect to server and return the socket fd if successful
  int valid = connect(socketfd, (struct sockaddr *) &server_address, sizeof(server_address));
  if (valid == -1) errorHandle(12, strerror(errno), 2);

  return socketfd;
}

int
main(int argc, char *argv[])
{
  // local variables in main
  int   opt;
  int   valid;
  ssize_t readSize;
  ssize_t writeSize;
  
  
  // option parsing
  while (1) {
    opt = getopt_long(argc, argv, "l:p:s:", long_options, NULL);
    if (opt == -1) break;

    switch (opt) {
    case 'l':
      logfile = fopen(optarg, "w+");
      if (logfile == NULL) errorHandle(5, strerror(errno), 2);
      break;
    case 'p':
      period = atoi(optarg);
      if (period < 1) errorHandle(1, optarg, 2);
      break;
    case 's':
      if      (optarg[0] == 'C') fahrenheit = 0;
      else if (optarg[0] == 'F') fahrenheit = 1;
      else errorHandle(2, optarg, 2);
      if (optarg[1] != '\0') errorHandle(2, optarg, 2);
      break;
    case 'i':
      id = strdup(optarg);
      if (strlen(id) != 9) errorHandle(10, id, 1);
      break;
    case 'h':
      host = strdup(optarg);
      break;
    default:
      errorHandle(0, argv[0], 1);
    }
  }

  // assign port number
  if (argc < 2) errorHandle(11, "N/A", 1);
  unsigned int i = 0;
  for (; i < strlen(argv[argc-1]); ++i) {
    if (argv[argc-1][i] == '\0') break;
    else if (!(isdigit(argv[argc-1][i]))) errorHandle(11, argv[argc-1], 1);
  }
  port = atoi(argv[argc-1]);

  // check if id, host, and port is set
  if (logfile == NULL || id == NULL || host == NULL || port < -1)
    errorHandle(0, argv[0], 1);

  // set up SSL encryption
  SSL_library_init();
  OpenSSL_add_all_algorithms();
  SSL_load_error_strings();
  ssl_ctx = SSL_CTX_new(TLSv1_client_method());
  if (ssl_ctx == NULL) errorHandle(16, "context create fail", 2);
  ssl = SSL_new(ssl_ctx);
  if (ssl == NULL) errorHandle(16, "ssl create fail", 2);
  
  
  // connect to the server and write the ID to server
  int socket_fd = client_connect();
  if (SSL_set_fd(ssl, socket_fd) == 0) errorHandle(16, "socket set fail", 2);
  if (SSL_connect(ssl) != 1) errorHandle(16, "ssl connect fail", 2);
  readSize = sprintf(buffer, "ID=%s\n", id);
  writeSize = SSL_write(ssl, buffer, readSize);
  if (readSize != writeSize) errorHandle(15, buffer, 2);
  
  // set signal, sensor, and button
  signal(SIGINT, sighandle);
  temp_sensor = mraa_aio_init(pinTempSensor);
  button = mraa_gpio_init(60);
  mraa_gpio_dir(button, MRAA_GPIO_IN);
  mraa_gpio_isr(button, MRAA_GPIO_EDGE_RISING, &button_pressed, NULL);

  // set poll for non-block read
  struct pollfd mypoll[1];
  mypoll[0].fd = socket_fd;
  mypoll[0].events = POLLIN | POLLHUP | POLLERR;

  // continue to loop until shutdown
  while (keepgoing) {
    // read from poll
    valid = poll(mypoll, 1, 0);
    if (valid == -1) errorHandle(6, strerror(errno), 2);

    // if stdin is triggered, process command
    if (mypoll[0].revents & POLLIN) {
      readSize = SSL_read(ssl, buffer, 1024);;
      if (readSize < 0) errorHandle(7, strerror(errno), 2);
      newParseCommand(buffer, readSize);
    }

    // print time and temperature
    if (notPause) {
      printNormal();
    }

    // sleep for set time
    sleep(period);
    
  }

  // close sensors
  mraa_aio_close(temp_sensor);
  mraa_gpio_close(button);

  // print the shudown msg
  printShutdown();

  valid = close(socket_fd);
  if (valid == -1) errorHandle(14, strerror(errno), 2);
  
  return 0;
}
