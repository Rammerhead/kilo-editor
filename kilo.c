/*** Includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>



/*** Includes ***/

struct termios orig_termios;


/*** Terminal ***/

void die(const char *s)
{
	perror(s);
	exit(1);
}

void disableRawMode()
{
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
		die("tcsetattr");
}

void enableRawMode()
{
	if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = orig_termios;
	//iflags: Input flags
	raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);	//Disable software flow control and carriage return translation
	
	//lflag: Local flags
	raw.c_lflag &= ~(ECHO | ICANON | ISIG);	//Disable echo, canonical mode, and sending SIGINT and SIGTSTP

	//oflag: Output flags
	raw.c_oflag &= ~(OPOST);

	//cflag: Control flags
	raw.c_cflag |= (CS8);

	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

/*** Init ***/

int main()
{
	enableRawMode();

	char c;
	while (1) {
		c = '\0';
		if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
		if (iscntrl(c)) {
			printf("%d\r\n", c);
		} else {
			printf("%d ( '%c')\r\n", c, c);
		}

		if (c == 'q') break;
	}


	return 0;
}
