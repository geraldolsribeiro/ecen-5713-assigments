/*
https://www.coursera.org/learn/linux-system-programming-introduction-to-buildroot/supplement/U1Beh/assignment-2-instructions

3. Write a C application “writer” (finder-app/writer.c)  which can be used as
an alternative to the “writer.sh” test script created in assignment1 and
using File IO as described in LSP chapter 2.

See the Assignment 1 requirements for the writer.sh test script and these
additional instructions:

One difference from the write.sh instructions in Assignment 1:  You do not need
to make your "writer" utility create directories which do not exist.  You can
assume the directory is created by the caller.

Setup syslog logging for your utility using the LOG_USER facility.

Use the syslog capability to write a message “Writing <string> to <file>” where
<string> is the text string written to file (second argument) and <file> is the
file created by the script.  This should be written with LOG_DEBUG level.

Use the syslog capability to log any unexpected errors with LOG_ERR level.
*/

#include <stdio.h>
#include <string.h>
#include <syslog.h>

int main( int argc, char **argv )
{
  openlog( "assessment 2", LOG_PERROR, LOG_USER );
  if( argc != 3 ) {
    syslog( LOG_ERR, "Wrong number of arguments: %d", argc );
    return 1;
  }

  FILE *fd = fopen( argv[1], "w" );
  if( !fd ) {
    syslog( LOG_ERR, "Error creating %s", argv[1] );
    return 1;
  }

  syslog( LOG_DEBUG, "Writing %s to %s", argv[2], argv[1] );
  fwrite( argv[2], 1, strlen( argv[2] ), fd );
  fclose( fd );

  return 0;
}
