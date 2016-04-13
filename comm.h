#ifndef PNP_COMM_H
#define PNP_COMM_H

enum	{NEW_FILE,	/* Send file descriptor to child */
	 CMD_EXIT,	/* Tell child to exit */
	 CMD_META,	/* Tell child to extract metadata */
	 CMD_PLAY,	/* Tell child to start playback */
	 MSG_ACK_FILE,	/* Acknowledge new file */
	 MSG_FILE_ERR,	/* Error reading file or malformed file */
	 MSG_WARN,	/* Warning */
	 MSG_FATAL,	/* Fatal error; child process exits */
	 META_ARTIST,	/* Child sends metadata */
	 META_TITLE,
	 META_ALBUM,
	 META_TRACKNO,
	 META_DATE,
	 META_TIME,
	 META_END};	/* End of metadata */

void		msg(int, void *, size_t);
void		msgstr(int, char *);
void		file_err(void);
__dead void	fatal(char *);
__dead void	_err(char *);
void		comm_init(int);

#endif
