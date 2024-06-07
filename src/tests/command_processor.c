#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>

#include "../commands.h"

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define assert_if(cond) for (; !cond; __assert(#cond, __FILE__, __LINE__))

#define READ_CMD(BUFF_FULL, HAVE_COMMAND, BUFF_CONTS, CMD)                  \
  assert(read_command(read_fd, commands_buffer, command_buffer,             \
                      MIN(sizeof(commands_buffer), sizeof(command_buffer)), \
                      &buffer_full, &have_command));                        \
  printf("Buffer full: %s, Have command: %s\n",                             \
         buffer_full ? "true" : "false", have_command ? "true" : "false");     \
  printf("Commands buffer: %s\n", commands_buffer);                         \
  printf("Command buffer: %s\n", command_buffer);                           \
  assert(buffer_full == BUFF_FULL);                                         \
  assert(have_command == HAVE_COMMAND);                                     \
  assert(strcmp(commands_buffer, BUFF_CONTS) == 0);                         \
  assert(strcmp(command_buffer, CMD) == 0)

#define WRITE(lit)                                                  \
  printf("Wiriting %s to pipe\n", #lit);                              \
  assert(write(write_fd, lit, sizeof(lit) - 1) == sizeof(lit) - 1); \
  fsync(write_fd)

#define SET_BUFFERS_READ(COMMANDS, COMMAND) \
  printf("Setting lines buffer to %s and line buffer to %s\n", #COMMANDS, #COMMAND); \
  strcpy(command_buffer, COMMAND); \
  strcpy(commands_buffer, COMMANDS)

void test_read_from_pipe() {
  char commands_buffer[16];
  commands_buffer[0] = '\0';
  int fd[2];
  assert(pipe(fd) != -1);
  int read_fd = fd[0];
  int write_fd = fd[1];
  fcntl(read_fd, F_SETFL, O_NONBLOCK);

  bool buffer_full, have_command = false;
  char command_buffer[16];
  SET_BUFFERS_READ("", "");
  WRITE("aa");
  READ_CMD(false, false, "aa", "");
  WRITE("\n");
  READ_CMD(false, false, "\n", "aa");
  READ_CMD(false, true, "", "aa");
  READ_CMD(false, false, "", "aa");
  WRITE("bbbbbbbbbbbbbb");
  READ_CMD(false, false, "bbbbbbbbbbbbbb", "aa");
  READ_CMD(true, false, "b", "aabbbbbbbbbbbbb");
  READ_CMD(true, false, "b", "aabbbbbbbbbbbbb");
  SET_BUFFERS_READ("b", "bbbbbbbbbbbbb");
  WRITE("\n\ni\n\n");
  READ_CMD(false, false, "\n\ni\n\n", "bbbbbbbbbbbbbb");
  READ_CMD(false, true, "\ni\n\n", "bbbbbbbbbbbbbb");
  command_buffer[0] = '\0';
  READ_CMD(false, true, "i\n\n", "");
  READ_CMD(false, true, "\n", "i");
  READ_CMD(false, true, "", "i");
}

#define EXTRACT_CMD(LINES, LINE, FITS, FULL_LINE)                     \
  fits = extract_line(lines_buffer, line_buffer, sizeof(line_buffer), \
                      &full_line);                                    \
  printf("Fits: %s, Full line: %s\n", fits ? "true" : "false",        \
         full_line ? "true" : "false");                               \
  printf("Line buffer: %s\n", line_buffer);                           \
  printf("Lines buffer: %s\n", lines_buffer);                         \
  assert(fits == FITS);                                               \
  assert(full_line == FULL_LINE);                                     \
  assert(strcmp(lines_buffer, LINES) == 0);                           \
  assert(strcmp(line_buffer, LINE) == 0)

#define SET_BUFFERS(LINES, LINE)                \
  printf("Setting lines buffer: %s\n", #LINES); \
  printf("Setting line buffer: %s\n", #LINE);   \
  strcpy(lines_buffer, LINES);                  \
  strcpy(line_buffer, LINE)

void test_extract_line() {
  char lines_buffer[12];
  char line_buffer[5];
  bool full_line;
  bool fits;
  printf("Lenght of line buffer: %ld\n", sizeof(line_buffer));
  printf("Lenght of lines buffer: %ld\n", sizeof(lines_buffer));
  SET_BUFFERS("012345", "ab");
  EXTRACT_CMD("2345", "ab01", false, false);

  SET_BUFFERS("01234", "");
  EXTRACT_CMD("4", "0123", false, false);

  SET_BUFFERS("012345\n", "");
  EXTRACT_CMD("45\n", "0123", false, false);

  SET_BUFFERS("01234\n", "");
  EXTRACT_CMD("4\n", "0123", false, false);

  SET_BUFFERS("0123\n", "");
  EXTRACT_CMD("", "0123", true, true);

  SET_BUFFERS("a\n", "daa");
  EXTRACT_CMD("", "daaa", true, true);

  SET_BUFFERS("a\nb\nc", "d");
  EXTRACT_CMD("b\nc", "da", true, true);
  EXTRACT_CMD("c", "dab", true, true);
  EXTRACT_CMD("", "dabc", true, false);

  SET_BUFFERS("aa", "");
  EXTRACT_CMD("", "aa", true, false);

  SET_BUFFERS("a", "daa");
  EXTRACT_CMD("", "daaa", true, false);

  SET_BUFFERS("ab", "a");
  EXTRACT_CMD("", "aab", true, false);

  SET_BUFFERS("012", "");
  EXTRACT_CMD("", "012", true, false);
}

int main() {
  // test_extract_line();
  test_read_from_pipe();
}