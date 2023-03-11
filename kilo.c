/*** Includes ***/
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)


/*** Data ***/

struct editorConfig {
	int cx, cy;						//Cursor position
	int screenrows, screencols;		//Window size
	struct termios orig_termios;
};

struct editorConfig E;


/*** Terminal ***/

//Panic button.
//Clears screen first, and then displays error
void die(const char *s)
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	
	perror(s);
	printf("\r\n");
	exit(1);
}

void disableRawMode()
{
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("tcsetattr");
}

void enableRawMode()
{
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;
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

// Low level terminal function which gets next char and returns it
char editorReadKey()
{
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) 
			die("read");
	}

	return c;
}

//Uses VT-100 DSR to get active position of the cursor
//and then writes that position in the standard input
//We then read that data into a buffer, and then check
//if the data read is an escape sequence
//If it is, we use sscanf to write that sequence into
//the row and col pointers
int getCursorPosition(int *rows, int *cols)
{
	char buf[32];
	unsigned int i = 0;

	//VT-100 DSR query
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

	//Reading the query into buffer until the R character is read
	//VT-100 documentation states that the position ends with R char
	while (i < sizeof(buf)-1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R') break;
		i++;
	}

	//Assigning 0 to the end of the buffer
	buf[i] = '\0';

	//Verify that the buffer contains an escape sequence
	if (buf[0] != '\x1b' || buf[1] != '[') return -1;

	//Using sscanf to send data in the buffer into pointers
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

	return 0;
}

// Returns window size. Returns -1 if unable to retrieve window size from terminal
// Uses ioctl to get the window size
// If ioctl fails, we use a fallback method of placing the cursor at the end of the terminal
// and then querying its position: C command queries to right, and B queries to left
// C and B commands stop cursor from going past edge of the screen
int getWindowSize(int *rows, int *cols)
{
	struct winsize ws;

	if (1 || ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return getCursorPosition(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/*** Buffers ***/

//Append buffer
struct abuf {
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0}

//Appends to the write buffer
void abAppend(struct abuf *ab, const char *s, int len)
{
	//Allocate memory of size len+current length
	char *new = realloc(ab->b, ab->len + len);

	//If the allocation failed, return
	if (new == NULL) return;

	//Copy from s to the write buffer
	memcpy(&new[ab->len], s, len);

	//Reassign pointer for write buffer
	ab->b = new;

	//Reassign length of write buffer
	ab->len += len;
}

void abFree(struct abuf *ab)
{
	free(ab->b);
}

/*** Output ***/

//Appends a '~' and a newline until the last line, where it only appends a '~'
//to prevent the terminal from scrolling over
void editorDrawRows(struct abuf *ab)
{
	int y;
	for (y = 0; y < E.screenrows-1; ++y){
		if (y == E.screenrows / 3) {
			char welcome[80];
			int welcomelen = snprintf(welcome, sizeof(welcome), 
					"Kilo editor -- version %s", KILO_VERSION);
			if (welcomelen > E.screencols) welcomelen = E.screencols;
			int padding = (E.screencols - welcomelen) / 2;
			if (padding) {
				abAppend(ab, "~", 1);
				padding--;
			}
			while (padding--) abAppend(ab, " ", 1);
			abAppend(ab, welcome, welcomelen);
		} else{
			abAppend(ab, "~", 1);
		}
		abAppend(ab, "\x1b[K", 3);

		if (y < E.screenrows -1)
			abAppend(ab, "\r\n", 2);
	}
}

// Refreshes the screen by redrawing things. 
// 0x1b is 27 -> Starts escape sequence, when followed by [
// 2 -> Entire screen, J -> Clear
// H -> Reposition cursor. Default: 1;1
void editorRefreshScreen()
{
	struct abuf ab = ABUF_INIT;
	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy+1, E.cx+1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/*** Input ***/


// Higher level processing function which makes use of editorReadkey 
// and takes action based on input character
void editorProcessKey()
{
	char c = editorReadKey();

	switch (c)
	{
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);
			exit(0);
			break;
	}
}

/*** Init ***/

//Gets window size of the terminal and sets them in the original config struct
void initEditor()
{
	E.cx = 0;
	E.cy = 0;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die ("getWindowSize");
}

int main()
{
	enableRawMode();

	initEditor();

	while (1) {
		editorRefreshScreen();
		editorProcessKey();		//Blocks loop flow
	}


	return 0;
}
