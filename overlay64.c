/*
overlay64 -- video overlay module
Copyright (C) 2016 Henning Bekel

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>

#include "target.h"

#if windows  
  #include <io.h>
  #include <fcntl.h>
#endif

#include "parser.h"
#include "usb.h"
#include "protocol.h"
#include "overlay64.h"

//------------------------------------------------------------------------------

#if linux
  const char* default_device = "/dev/overlay64";
#else
  const char* default_device = "usb";
#endif

char *device;
DeviceInfo overlay64;
DeviceInfo usbasp;

extern uint16_t written;

//------------------------------------------------------------------------------

int main(int argc, char **argv) {  

  int result = EXIT_SUCCESS;
  
  device = (char*) calloc(strlen(default_device)+1, sizeof(char));
  strcpy(device, default_device);

  struct option options[] = {
    { "help",     no_argument,       0, 'h' },
    { "version",  no_argument,       0, 'v' },
    { "device",   required_argument, 0, 'd' },
    { 0, 0, 0, 0 },
  };
  int option, option_index;
  
  while(1) {
    option = getopt_long(argc, argv, "hvd:i", options, &option_index);

    if(option == -1)
      break;
    
    switch (option) {
      
    case 'h':
      usage();
      goto done;
      break;      
      
    case 'v':
      version();
      goto done;
      break;
      
    case 'd':
      device = (char*) realloc(device, strlen(optarg)+1);
      strcpy(device, optarg);
      break;
      
    case '?':
    case ':':
      goto done;
    }    
  }
  
  argc -= optind;
  argv += optind;

  prepare_devices();

  if(argc) {
    
    if(strncmp(argv[0], "convert", 7) == 0) {
      result = convert(--argc, ++argv);
    }
    else if(strncmp(argv[0], "configure", 9) == 0) {
      result = configure(--argc, ++argv);
    }
    else if(strncmp(argv[0], "update", 6) == 0) {
      result = update(--argc, ++argv);
    }
    else if(strncmp(argv[0], "boot", 4) == 0) {
      result = boot() ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    else if(strncmp(argv[0], "identify", 2) == 0) {
      result = identify();
    }
  }
  else {
    usage();
    result = EXIT_FAILURE;
  }
  
 done:
  free(device);
  return result;
}

//------------------------------------------------------------------------------

int convert(int argc, char **argv) {

  int result = EXIT_FAILURE;

  FILE *in  = stdin;
  FILE *out = stdout;
  Format output_format = BINARY;
  uint16_t footprint;

  config = Config_new();
  
  if(argc >= 1 && (strncmp(argv[0], "-", 1) != 0)) {

    if((in = fopen(argv[0], "rb")) == NULL) {
      fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
      goto done;
    }
  }

  if(argc >= 2 && (strncmp(argv[1], "-", 1) != 0)) {

    if((out = fopen(argv[1], "wb")) == NULL) {
      fprintf(stderr, "%s: %s\n", argv[1], strerror(errno));
      goto done;
    }
    if(strncasecmp(argv[1]+(strlen(argv[1])-5), ".conf", 5) == 0) {
      output_format = CONFIG;
    }
  }

  if(in == stdin) {
    fprintf(stderr, "reading from stdin...\n");
#if windows
    setmode(_fileno(stdin), O_BINARY);
#endif
  }

  if(out == stdout) {
    fprintf(stderr, "writing to stdout...\n");
#if windows
    setmode(_fileno(stdout), O_BINARY);
#endif
  }
  
  if(Config_read(config, in) || Config_parse(config, in)) {
    
    output_format == BINARY ?
      Config_write(config, out) :
      Config_print(config, out);

    footprint = Config_get_footprint(config);
    
    fprintf(stderr, "EEPROM:\t%5d of  4096 bytes used (%5d bytes free)\n",
            written, 4096-written);    
  
    fprintf(stderr, "SRAM:\t%5d of 16384 bytes used (%5d bytes free)\n",
            footprint, 16384-footprint);
    
    result = EXIT_SUCCESS;
  }

 done:
  Config_free(config);
  return result;
}

//------------------------------------------------------------------------------

int configure(int argc, char **argv) {

  int result = EXIT_FAILURE;

  FILE *in  = stdin;
  FILE *out = NULL;
  uint8_t *data = NULL;
  uint16_t size = 0;

  config = Config_new();
  
  if(argc >= 1 && (strncmp(argv[0], "-", 1) != 0)) {

    if((in = fopen(argv[0], "rb")) == NULL) {
      fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
      goto done;
    }
  }

  if(in == stdin) {
    fprintf(stderr, "reading from stdin...\n");
#if windows
    setmode(_fileno(stdin), O_BINARY);
#endif    
  }

  fprintf(stderr, "Converting %s...\n", argv[0]);
  
  if(Config_read(config, in) || Config_parse(config, in)) {

    data = (uint8_t*) calloc(4096, sizeof(char));
  
    if((out = fmemopen(data, 4096, "wb")) == NULL) {
      fprintf(stderr, "error: %s\n", strerror(errno));
      goto done;
    }
  
    Config_write(config, out);
    size = ftell(out);
    fmemupdate(out, data, size);  
    fclose(out);

    fprintf(stderr, "Created binary configuration: %d bytes\n", size);
    
    result = program(USBASP_WRITEEEPROM, data, size);
  }

 done:  
  Config_free(config);
  fclose(in);

  if(data != NULL) free(data);
  return result;
}

//------------------------------------------------------------------------------

int update(int argc, char **argv) {
  int result = EXIT_FAILURE;
  FILE *in;
  uint8_t *data = NULL;
  uint16_t size = 0;
  
  struct stat st;
  
  if(!argc) {
    usage();
    goto done;
  }
  
  if((in = fopen(argv[0], "rb")) == NULL) {
    goto error;
  }

  if(fstat(fileno(in), &st) == -1) {
    goto error;
  }

  size = st.st_size;
  data = (uint8_t *) calloc(size, sizeof(uint8_t));

  if(fread(data, sizeof(uint8_t), size, in) != size) {
    goto error;
  }
  fclose(in);

  result = program(USBASP_WRITEFLASH, data, size);
  
 done:
  if(data != NULL) free(data);
  return result;

 error:
  fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
  goto done;  
}

//------------------------------------------------------------------------------

int program(int command, uint8_t *data, int size)  {
  
  if(boot()) {
    usb_control(&usbasp, USBASP_CONNECT);
  
    for(uint32_t i=0; i<size+64; i+=64) {
      usb_send(&usbasp, command,
               (uint16_t) (i & 0xffff), (uint16_t) (i>>16),
               data+i, 64);
      fprintf(stderr, "\rUpdating %s: %d of %d bytes transferred...",
              command == USBASP_WRITEFLASH ? "application" : "configuration",
              (i<size) ? i : size , size);
    }
    fprintf(stderr, "ok\n");

    usb_quiet = true;  
    usb_control(&usbasp, USBASP_DISCONNECT);
  }
  else {
    fprintf(stderr, "error: could not connect to usbasp bootloader\n");
    return false;
  }  
  return true;
}

//------------------------------------------------------------------------------

int boot(void) {
  if(!usb_ping(&overlay64)) {
    return false;
  }

  fprintf(stderr, "Entering bootloader"); fflush(stderr);
  usb_control(&overlay64, OVERLAY64_BOOT);
  
  uint8_t tries = 10;
  usb_quiet = true;
  while(!usb_ping(&usbasp)) {
    fprintf(stderr, "."); fflush(stderr);
    
#if windows 
    Sleep(1000);
#else
    sleep(1);
#endif
    
    if(!--tries) {
      return false;
    }
  }
  fprintf(stderr, "OK\n");
  return true;
}

//------------------------------------------------------------------------------

bool identify(void) {
  char id[64];
  
  if(usb_receive(&overlay64, OVERLAY64_IDENTIFY, 0, 0, (uint8_t*) id, 64) > 0) {
    printf("%s\n", id);
    return true;
  }
  return false;
}

//------------------------------------------------------------------------------

void prepare_devices(void) {
  strncpy(overlay64.path, device, 4096);
  overlay64.vid = OVERLAY64_VID;
  overlay64.pid = OVERLAY64_PID;

#if linux
  strncpy(usbasp.path, "/dev/usbasp", 4096);
#else
  strncpy(usbasp.path, "usbasp", 4096);
#endif
  usbasp.vid = USBASP_VID;
  usbasp.pid = USBASP_PID;
}

//------------------------------------------------------------------------------

#if defined(WIN32) && !defined(__CYGWIN__)
FILE* fmemopen(void *__restrict buf, size_t size, const char *__restrict mode) {

  FILE* result;
  char path[MAX_PATH+1]; 
  char file[MAX_PATH+1];

  if(!GetTempPath(MAX_PATH+1, path)) return NULL;
  if(!GetTempFileName(path, "key", 0, file)) return NULL;

  result = fopen(file, "wbD+");
  fwrite(buf, sizeof(char), size, result);
  fseek(result, 0, SEEK_SET);
  
  return result;
}
#endif

//------------------------------------------------------------------------------

void fmemupdate(FILE *fp, void *buf,  uint16_t size) {
#if defined(WIN32) && !defined(__CYGWIN__)
  fseek(fp, 0, SEEK_SET);
  fread(buf, sizeof(uint8_t), size, fp);
#endif
}

//------------------------------------------------------------------------------

void version(void) {
  printf("Overlay64 v%.1f\n", VERSION);    
  printf("Copyright (C) 2016 Henning Bekel.\n");
  printf("License GPLv3: GNU GPL version 3 <http://gnu.org/licenses/gpl.html>.\n");
  printf("This is free software: you are free to change and redistribute it.\n");
  printf("There is NO WARRANTY, to the extent permitted by law.\n");
}

//------------------------------------------------------------------------------

void usage(void) {
  version();
  printf("\n");
  printf("Usage:\n");
  printf("      overlay64 <options>\n");
  printf("      overlay64 <options> convert [<infile>|-] [<outfile>|-]\n");
  printf("      overlay64 <options> configure [<infile>]\n");
  printf("      overlay64 <options> update <firmware>\n");
  printf("      overlay64 <options> identify\n");      
  printf("\n");
  printf("  Options:\n");
  printf("           -v, --version  : print version information\n");
  printf("           -h, --help     : print this help text\n");
#if linux
  printf("           -d, --device   : specify usb device (default: /dev/overlay64)\n");
#elif windows
  printf("           -d, --device   : specify usb device (default: usb)\n");
#endif
  printf("\n");
  printf("  Files:\n");
  printf("           <infile>   : input file, format is autodetected\n");
  printf("           <outfile>  : output file, format determined by extension\n");
  printf("           <firmware> : binary firmware image\n");    
  printf("\n");
  printf("           *.conf : plain text config file format\n");
  printf("           *.bin  : binary file format (default)\n");  
  printf("\n");
  printf("           Optional arguments default to stdin or stdout\n");
  printf("\n");
}

//------------------------------------------------------------------------------
