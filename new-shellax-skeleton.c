#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#define READ_END 0
#define WRITE_END 1
// Esma Eray 71719, Omer Atasoy 71507
const char *sysname = "shellax";

enum return_codes {
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t {
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next) {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}
/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
  if (command->arg_count) {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next) {
    free_command(command->next);
    command->next = NULL;
  }
  free(command->name);
  free(command);
  return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
  char cwd[1024], hostname[1024];
  gethostname(hostname, sizeof(hostname));
  getcwd(cwd, sizeof(cwd));
  printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
  return 0;
}
/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
  const char *splitters = " \t"; // split at whitespace
  int index, len;
  len = strlen(buf);
  while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
  {
    buf++;
    len--;
  }
  while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
    buf[--len] = 0; // trim right whitespace

  if (len > 0 && buf[len - 1] == '?') // auto-complete
    command->auto_complete = true;
  if (len > 0 && buf[len - 1] == '&') // background
    command->background = true;
  char *pch = strtok(buf, splitters);
  if (pch == NULL) {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  } else {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1) {
    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue; // empty arg, go for next
    while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
    {
      arg++;
      len--;
    }
    while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
      arg[--len] = 0; // trim right whitespace
    if (len == 0)
      continue; // empty arg, go for next

    // piping to another command
    if (strcmp(arg, "|") == 0) {
      struct command_t *c = malloc(sizeof(struct command_t));
      int l = strlen(pch);
      pch[l] = splitters[0]; // restore strtok termination
      index = 1;
      while (pch[index] == ' ' || pch[index] == '\t')
        index++; // skip whitespaces

      parse_command(pch + index, c);
      pch[l] = 0; // put back strtok termination
      command->next = c;
      continue;
    }

    // background process
    if (strcmp(arg, "&") == 0)
      continue; // handled before

    // handle input redirection
    redirect_index = -1;
    if (arg[0] == '<')
      redirect_index = 0;
    if (arg[0] == '>') {
      if (len > 1 && arg[1] == '>') {
        redirect_index = 2;
        arg++;
        len--;
      } else
        redirect_index = 1;
    }
    if (redirect_index != -1) {
      command->redirects[redirect_index] = malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;
  return 0;
}

void prompt_backspace() {
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
  int index = 0;
  char c;
  char buf[4096];
  static char oldbuf[4096];

  // tcgetattr gets the parameters of the current terminal
  // STDIN_FILENO will tell tcgetattr that it should write the settings
  // of stdin to oldt
  static struct termios backup_termios, new_termios;
  tcgetattr(STDIN_FILENO, &backup_termios);
  new_termios = backup_termios;
  // ICANON normally takes care that one line at a time will be processed
  // that means it will return if it sees a "\n" or an EOF or an EOL
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1) {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0) {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0) {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

    putchar(c); // echo the character
    buf[index++] = c;
    if (index >= sizeof(buf) - 1)
      break;
    if (c == '\n') // enter key
      break;
    if (c == 4) // Ctrl+D
      return EXIT;
  }
  if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
    index--;
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  // print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}
int process_command(struct command_t *command);
int main() {
  while (1) {
    struct command_t *command = malloc(sizeof(struct command_t));
    memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

    int code;
    code = prompt(command);
    if (code == EXIT)
      break;

    code = process_command(command);
    if (code == EXIT)
      break;

    free_command(command);
  }

  printf("\n");
  return 0;
}

void moodprinter(struct command_t *command){
  int input = -1;
  int error;
  printf("How is your mood today?\n");
  printf("1. happy\n");
  printf("2. sad\n");
  printf("3. angry\n");
  printf("4. rageous\n");
  printf("5. suspicious\n");
  printf("6. surprised\n");
  printf("7. bored\n");
  printf("8. full of love\n");
  printf("9. cheerful\n");
  printf("10. stunned\n");
  while (input < 1 || input > 10){
    printf("Choose one of these numbers: ");
    error = scanf("%d", &input);
    if (error != 1) {
      break;
    }
  }
  if (input > 0 && input < 11){
    printf("This is your mood emoji: ");
  }
  switch (input) {
	  case 1:
		  printf("[^w^]\n");
		  break;
	  case 2:
		  printf("( T~~T )\n");
		  break;
	  case 3:
		  printf("•`_´•\n");
		  break;
	  case 4:
		  printf("( ╯°□°)╯┻━━┻\n");
		  break;
	  case 5:
		  printf("( `_>´)\n");
		  break;
	  case 6:
		  printf("( 0 _ 0 )\n");
		  break;
	  case 7:
		  printf("(-_-)\n");
		  break;
	  case 8:
		  printf("( ˘ ³˘)\n");
		  break;
	  case 9:
		  printf("\\(^o^)/\n");
		  break;
	  case 10:
		  printf("(*_*)\n");
		  break;
	  default:
		  printf("invalid input\n");
  }
}

void lsfiles(struct command_t *command){
  // system("python3 lsfiles.py");
  char *py = "/usr/bin/python3";
  char *ls = "./lsfiles.py";
  char *args[] = {py, ls, NULL};
  pid_t pypid = fork();
  if (pypid == 0){
    execv(py, args);
    perror("python\n");
  }
  else {
    wait(NULL);
  }
}

void wiseman(struct command_t *command){
  if(command->arg_count != 1){
    fprintf(stderr, "%d arguments in wiseman. 1 needed\n", command->arg_count);
    return;
  }
  if (atoi(command->args[0]) <= 0){
    fprintf(stderr, "Please enter a positive integer. You entered %s\n", command->args[0]);
    return;
  }
  char cron[256];
  int oldStdout;
  int oldStdin;
  int newStdin;
  int newStdout;
  int tempcron;
  sprintf(cron, "*/%s * * * * /usr/games/fortune | /usr/games/cowsay >> /tmp/wisecow.txt\n", command->args[0]);
  oldStdout = dup(STDOUT_FILENO);
  oldStdin = dup(STDIN_FILENO);
  newStdout = open("tempcron", O_CREAT | O_RDWR | O_TRUNC, 0777);
  dup2(newStdout, STDOUT_FILENO);
  close(newStdout);
  system("crontab -l");
  dup2(oldStdout, STDOUT_FILENO);
  close(oldStdout);
  
  tempcron = open("tempcron", O_RDWR | O_APPEND);
  write(tempcron, cron, strlen(cron));
  close(tempcron);

  system("crontab tempcron");

  remove("tempcron");
  
}

void chatroom(struct command_t *command){
  if (command->arg_count != 2) {
    fprintf(stderr, "%d arguments in chatroom. 2 needed\n", command->arg_count);
    return;
  }
  FILE *stdinfp = stdin;
  int fifoRead, fifoWrite;
  char readMsg[500] = "";
  char *writeMsg;
  char longWriteMsg[500] = "";
  size_t len = 0;
  const char *roomname_short = command->args[0];
  const char *username_short = command->args[1];
  char roomname[500] = "";
  char username[500] = "";
  strcat(roomname, "/tmp/chatroom-");
  strcat(roomname, roomname_short);
  strcpy(username, roomname);
  strcat(username, "/");
  strcat(username, username_short);
  mkdir("/tmp", 0755); // fails if directory exists
  mkdir(roomname, 0755); // fails if directory exists
  int errorFifo = mkfifo(username, 0666);
  printf("Welcome to %s!\n", roomname_short);
  struct dirent *users;
  DIR *roomFolder;
  pid_t pidWrite;
  pid_t pid = fork();
  if (pid == 0){
    // Child
    while (1){
      // READ
      printf("\r[%s] %s > ", roomname_short, username_short);
      fflush(stdout);
      fifoRead = open(username, O_RDONLY);
      read(fifoRead, readMsg, sizeof(readMsg));
      printf("\r[%s] %s",roomname_short, readMsg);
    }
  } 
  else {
    // Parent
    while(1){
      // WRITE
      roomFolder = opendir(roomname);
      if (roomFolder == NULL) {
        perror("error with opening directory\n");
        return;
      }
      printf("\r[%s] %s > ", roomname_short, username_short);
      fflush(stdout);
      getline(&writeMsg, &len, stdinfp);
      while ((users = readdir(roomFolder)) != NULL){
	if (users->d_type != 1) {
          continue;
	}
	if (strcmp(users->d_name, username_short) == 0){
	  continue;
	}
	char longUsername[500] = "";
	strcpy(longUsername, roomname);
	strcat(longUsername, "/");
	strcat(longUsername, users->d_name);
	pidWrite = fork();
	if (pidWrite == 0){
          // Child Write
	  strcpy(longWriteMsg, username_short);
	  strcat(longWriteMsg, ": ");
	  strcat(longWriteMsg, writeMsg);
	  fifoWrite = open(longUsername, O_WRONLY);
	  write(fifoWrite, longWriteMsg, sizeof(longWriteMsg));
          exit(0);
	}
	else {
	  // Parent Write
	  waitpid(pidWrite, NULL, 0);
	}
      }
      closedir(roomFolder);
    }
  }
}

void uniq(struct command_t *command){
  bool cFlag = false;
  if (command->arg_count>0 && (strcmp(command->args[0], "--count") == 0 || strcmp(command->args[0], "-c") == 0)){
    cFlag = true;
  }
  FILE *fp = stdin;
  char *current = NULL;
  char *next = NULL;
  size_t len = 0;
  ssize_t error;
  ssize_t error2;
  int count = 1;
  error = getline(&current, &len, fp);
  while((error = getline(&next, &len, fp)) >= 0){
    if(strcmp(current, next) == 0){
      count++;
      continue;
    }
    else{
      if (cFlag){
        printf("%6d %s", count, current);
      }
      else { 
        printf("%s", current);
      }
      count = 1;
      strcpy(current, next);
    }
  }
  if (strlen(next)>0){
    if (cFlag){
      printf("%6d %s", count, current);
    }
    else { 
      printf("%s", current);
    }
  }
  else {
    if (cFlag){
      printf("%6d %s", count, current);
    }
    else { 
      printf("%s", current);
    }
  }
  if(current){
    free(current);
  }
  if(next){
    free(next);
  }
}

int myPipe(struct command_t *command){ 
      if (!(command->next)) return 10;
      int pipefd[2];
      if (pipe(pipefd) == -1){
	      perror("Error in pipe pipe\n");
	      return 1;
      }
      int pipepid1 = fork();
      if (pipepid1 < 0) {
	      perror("Error in pipe fork 1\n");
	      return 1;
      }
      if (pipepid1 == 0){
	      dup2(pipefd[WRITE_END], STDOUT_FILENO);
	      close(pipefd[READ_END]);
	      close(pipefd[WRITE_END]);
	      char *program_path = malloc(500);
	      strcat(program_path, "/usr/bin/");
	      strcat(program_path, command->name);
	      execv(program_path, command->args);
	      // execvp(command->name, command->args); // exec+args+path    
	      // execv has returned, the program path was not found, memory leak risk
	      free(program_path);
	      exit(0);
      }
      int pipepid2 = fork();
      if (pipepid2 < 0) {
	      perror("Error in pipe for 2\n");
	      return 1;
      }
      if (pipepid2 == 0) {
	      dup2(pipefd[READ_END], STDIN_FILENO);
	      close(pipefd[READ_END]);
	      close(pipefd[WRITE_END]);
	      
              command = command->next; 
	      process_command(command);
	      exit(0);
      }

      close(pipefd[READ_END]);
      close(pipefd[WRITE_END]);
      waitpid(pipepid1, NULL, 0);
      waitpid(pipepid2, NULL, 0);
      exit(0);
}

int myRedirect(struct command_t *command){ 
    if (command->redirects[0]){
      //printf("< : %s\n", command->redirects[0]);
      const char* inputfile = command->redirects[0];
      if (inputfile){
        int fd = open(inputfile, O_RDONLY, 0666);
	int error = dup2(fd, STDIN_FILENO);
	if (error == -1){
		perror("Error in redirect 0\n");
		return 1;
	}
      }
    }
    if (command->redirects[1]){ 
      //printf("> : %s\n", command->redirects[1]);
      
      const char* outputfile = command->redirects[1];
      if (outputfile){
        int fd = open(outputfile, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	int error = dup2(fd, STDOUT_FILENO);
	if (error == -1){
		perror("Error in redirect 1\n");
		return 1;
	}
      }
    }
    if (command->redirects[2]){ 
      //printf(">> : %s\n", command->redirects[2]);
      
      const char* appendfile = command->redirects[2];
      if (appendfile){
        int fd = open(appendfile, O_CREAT | O_WRONLY | O_APPEND, 0666);
	int error = dup2(fd, STDOUT_FILENO);
	if (error == -1){
		perror("Error in redirect 0\n");
		return 1;
	}
      }
    }
    return 0;
}

int process_command(struct command_t *command) {
  int r;
  if (strcmp(command->name, "") == 0)
    return SUCCESS;

  if ((strcmp(command->name, "exit") == 0) || (strcmp(command->name, "quit") == 0) || (strcmp(command->name, "q") == 0))
    return EXIT;

  if (strcmp(command->name, "cd") == 0) {
    if (command->arg_count > 0) {
      r = chdir(command->args[0]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }

  if (strcmp(command->name, "uniq") == 0) {
    uniq(command); 
    int redirectRet = myRedirect(command);
    if (redirectRet != 0) return redirectRet;
    // I/O redirection
    // <: 0, >: 1, <<: 2
    // handle pipes
    int pipeRet = myPipe(command); // function
    if(pipeRet != 10) return pipeRet;
    return SUCCESS;
  }

  if (strcmp(command->name, "chatroom") == 0) {
    chatroom(command);
    return SUCCESS;
  }

  if (strcmp(command->name, "wiseman") == 0){
    wiseman(command);
    return SUCCESS;
  }

  if (strcmp(command->name, "lsfiles") == 0){
    lsfiles(command);
    return SUCCESS;
  }

  if (strcmp(command->name, "moodprinter") == 0){
    moodprinter(command);
    return SUCCESS;
  }

  pid_t pid = fork();
  if (pid == 0) // child
  {
    /// This shows how to do exec with environ (but is not available on MacOs)
    // extern char** environ; // environment variables
    // execvpe(command->name, command->args, environ); // exec+args+path+environ

    /// This shows how to do exec with auto-path resolve
    // add a NULL argument to the end of args, and the name to the beginning
    // as required by exec

    // increase args size by 2
    command->args = (char **)realloc(
        command->args, sizeof(char *) * (command->arg_count += 2));

    // shift everything forward by 1
    for (int i = command->arg_count - 2; i > 0; --i)
      command->args[i] = command->args[i - 1];

    // set args[0] as a copy of name
    command->args[0] = strdup(command->name);
    // set args[arg_count-1] (last) to NULL
    command->args[command->arg_count - 1] = NULL;
    // I/O redirection
    // <: 0, >: 1, <<: 2
    int redirectRet = myRedirect(command);
    if (redirectRet != 0) return redirectRet;
    // handle pipes
    int pipeRet = myPipe(command); // function
    if(pipeRet != 10) return 1;
    
    // do your own exec with path resolving using execv()
    // do so by replacing the execvp call below
    
    
    char *program_path = malloc(500);
    strcat(program_path, "/usr/bin/");
    strcat(program_path, command->name);
    execv(program_path, command->args);
    // execvp(command->name, command->args); // exec+args+path
    
    // execv has returned, the program path was not found, memory leak risk
    free(program_path);
    exit(0);
    
  } else {
    // implement background processes here
    if (!command->background){
      waitpid(pid, NULL, 0); // wait for child process to finish
    }
    else{
      printf("Executing in the background. PID is: %d\n", (int) pid);
    }
    return SUCCESS;
  }

  // TODO: your implementation here

  printf("-%s: %s: command not found\n", sysname, command->name);
  return UNKNOWN;
}
