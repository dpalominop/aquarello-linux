#include "tcpserver.h"

extern "C"{
#include "cuftpd.h"
}

/* local functions declarations */
extern "C" {static int cuftpd_do_user(int,char*);
static int cuftpd_do_pass(int,char*);
static int cuftpd_do_pwd(int, char*);
static int cuftpd_do_cwd(int, char*);
static int cuftpd_do_syst(int,char*);
static int cuftpd_do_list(int,char*);
static int cuftpd_do_size(int,char*);
static int cuftpd_do_dele(int,char*);
static int cuftpd_do_type(int,char*);
static int cuftpd_do_port(int,char*);
static int cuftpd_do_pasv(int,char*);
static int cuftpd_do_retr(int,char*);
static int cuftpd_do_stor(int,char*);
static int cuftpd_do_quit(int,char*);
int checkImageExten(char*);
char *getFieldXML(char *, char *);
}
//extern "C" {int getsockname (int s, struct sockaddr * restrict name, socklen_t * restrict namelen);}
/* global variables */
//extern "C" {void emitSignal();}
int cuftpd_debug_on = 0;
int cuftpd_srv_port = CUFTPD_DEF_SRV_PORT;
int cuftpd_cur_child_num;
int cuftpd_quit_flag;
struct cuftpd_user_struct *cuftpd_cur_user;
int cuftpd_pasv_fd = -1;
int cuftpd_pasv_connfd = -1;
int cuftpd_port_connfd = -1;
char cuftpd_home_dir[PATH_MAX];
char filename[50];
int enable_rw=0;
int putfile=0;
/*
* Currently supported ftp commands.
*/

struct cuftpd_cmd_struct cuftpd_cmds[] = {
        {(char *)"USER", cuftpd_do_user},
        {(char *)"PASS", cuftpd_do_pass},
        {(char *)"PWD",  cuftpd_do_pwd},
        {(char *)"XPWD", cuftpd_do_pwd},
        {(char *)"CWD",  cuftpd_do_cwd},
        {(char *)"LIST", cuftpd_do_list},
        {(char *)"NLST", cuftpd_do_list},
        {(char *)"SYST", cuftpd_do_syst},
        {(char *)"TYPE", cuftpd_do_type},
        {(char *)"SIZE", cuftpd_do_size},
        {(char *)"DELE", cuftpd_do_dele},
        {(char *)"RMD",  cuftpd_do_dele},
        {(char *)"PORT", cuftpd_do_port},
        {(char *)"PASV", cuftpd_do_pasv},
        {(char *)"RETR", cuftpd_do_retr},
        {(char *)"STOR", cuftpd_do_stor},
        {(char *)"QUIT", cuftpd_do_quit},
        { NULL,         NULL}
};

/*
* Anonymous users, you can add more user/pass pairs here.
*/
struct cuftpd_user_struct cuftpd_users[] = {
/*        {"anonymous", ""},*/
        {"ftp", ""}
};

/*
* Ftp server's responses, and are not complete.
*/
char cuftpd_srv_resps[][256] = {
        "150 Begin transfer"                                                        CUFTPD_LINE_END,
        "200 OK"                                                                                CUFTPD_LINE_END,
        "213 %d"                                                                                CUFTPD_LINE_END,
        "215 UNIX Type: L8"                                                                CUFTPD_LINE_END,
        "220 CuFTPD " CUFTPD_VER " Server"                                CUFTPD_LINE_END,
        "221 Goodbye"                                                                        CUFTPD_LINE_END,
        "226 Transfer complete"                                                        CUFTPD_LINE_END,
        "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)"        CUFTPD_LINE_END,
        "230 User %s logged in"                                                        CUFTPD_LINE_END,
        "250 CWD command successful"                                        CUFTPD_LINE_END,
        "257 \"%s\" is current directory"                                CUFTPD_LINE_END,
        "331 Password required for %s"                                        CUFTPD_LINE_END,
        "500 Unsupport command %s"                                                CUFTPD_LINE_END,
        "530 Login %s"                                                                        CUFTPD_LINE_END,
        "550 Error"                                                                                CUFTPD_LINE_END
};



static void
cuftpd_debug(const char *file, int line, const char *fmt, ...)
{
        va_list ap;

        if (!cuftpd_debug_on)
                return;

        fprintf(stderr, "(%s:%d:%d)", file, line, getpid());
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
}
/*
static void
cuftpd_usage(void)
{
        printf("cuftpd " CUFTPD_VER " usage:\n");
        printf("cuftpd -p port -d\n");
}

static void
cuftpd_parse_args(int argc, char *argv[])
{
        int opt;
        int err = 0;

        while ((opt = getopt(argc, argv, "p:dh")) != -1) {
                switch (opt) {
                        case 'p':
                            cuftpd_srv_port = atoi(optarg);
                                err = (cuftpd_srv_port < 0 || cuftpd_srv_port > 65535);
                            break;
                        case 'd':
                            cuftpd_debug_on = 1;
                            break;
                        default:
                            err = 1;
                            break;
                }

                if (err) {
                        cuftpd_usage();
                        exit(-1);
                }
        }

        CUFTPD_DEBUG("srv port:%d\n", cuftpd_srv_port);
}
*/
static void
cuftpd_sigchild(int signo)
{
        int status;

        if (signo != SIGCHLD)
                return;

        while (waitpid(-1, &status, WNOHANG) > 0)
                cuftpd_cur_child_num--;

        CUFTPD_DEBUG("caught a SIGCHLD, cuftpd_cur_child_num=%d\n", cuftpd_cur_child_num);
}
static int
cuftpd_init(void)
{
        /* add all init statements here */

        signal(SIGPIPE, SIG_IGN);
        signal(SIGCHLD, cuftpd_sigchild);
        chdir(HOME_DIR);
        getcwd(cuftpd_home_dir, sizeof(cuftpd_home_dir));
        //sprintf(cuftpd_home_dir,"%s","/media/mmcblk0p4/aquarello/slideshow");
        return CUFTPD_OK;
}

/*
* Create ftp server's listening socket.
*/
static int
cuftpd_create_srv(void)
{
        int fd;
        int on = 1;
        int err;
        struct sockaddr_in srvaddr;

        if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                CUFTPD_DEBUG("socket() failed: %s\n", strerror(errno));
                return fd;
        }

        err = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        if (err < 0) {
                CUFTPD_DEBUG("setsockopt() failed: %s\n", strerror(errno));
                close(fd);
                return CUFTPD_ERR;
        }

        memset(&srvaddr, 0, sizeof(srvaddr));
        srvaddr.sin_family = AF_INET;
        srvaddr.sin_port = htons(cuftpd_srv_port);
        srvaddr.sin_addr.s_addr = htonl(INADDR_ANY);
        err = bind(fd, (struct sockaddr*)&srvaddr, sizeof(srvaddr));
        if (err < 0) {
                CUFTPD_DEBUG("bind() failed: %s\n", strerror(errno));
                close(fd);
                return CUFTPD_ERR;
        }
        err = listen(fd, CUFTPD_LISTEN_QU_LEN);
        if (err < 0) {
                CUFTPD_DEBUG("listen() failed: %s\n", strerror(errno));
                close(fd);
                return CUFTPD_ERR;
        }

        if (cuftpd_debug_on) {
                 int len = sizeof(srvaddr);
                 getsockname(fd, (struct sockaddr*)&srvaddr, (socklen_t*)&len);
                 CUFTPD_DEBUG("create srv listen socket OK: %s:%d\n",inet_ntoa(srvaddr.sin_addr), ntohs(srvaddr.sin_port));
        }

        return fd;

}
/*
* Map server response's number to the msg pointer.
*/
static char *
cuftpd_srv_resp_num2msg(int num)
{
        int i;
        char buf[8];

        snprintf(buf, sizeof(buf), "%d", num);
        if (strlen(buf) != 3)
                return NULL;

        for (i = 0; i < (int)CUFTPD_ARR_LEN(cuftpd_srv_resps); i++)
                if (strncmp(buf, cuftpd_srv_resps[i], strlen(buf)) == 0)
                        return cuftpd_srv_resps[i];

        return NULL;
}

static int
cuftpd_send_msg(int fd, char *msg, int len)
{
        int n, off = 0, left = len;

        while (1) {
                n = write(fd, msg + off, left);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        return n;
                }
                if (n < left) {
                        left -= n;
                        off += n;
                        continue;
                }
                return len;
        }
}
static int
cuftpd_recv_msg(int fd, char buf[], int len)
{
        int n;
        struct timeval mytimeout;
        fd_set r;
        int retval;
        mytimeout.tv_sec=CUFTPD_TIMEOUT;
        mytimeout.tv_usec=0;

        FD_ZERO(&r);
        FD_SET(fd,&r);

        retval=select(fd+1,&r,NULL,NULL,&mytimeout);

        if(retval)
        {
            while (1) {
                n = read(fd, buf, len);
                if (n < 0) {
                        if (errno == EINTR)
                                continue;
                        return n;
                }
                return n;
            }
        }
	return -1;
}

static int
cuftpd_send_resp(int fd, int num, ...)
{
        char *cp = cuftpd_srv_resp_num2msg(num);
        va_list ap;
        char buf[BUFSIZ];

        if (!cp) {
                CUFTPD_DEBUG("cuftpd_srv_resp_num2msg(%d) failed\n", num);
                return CUFTPD_ERR;
        }

        va_start(ap, num);
        vsnprintf(buf, sizeof(buf), cp, ap);
        va_end(ap);

        CUFTPD_DEBUG("send resp:%s\n", buf);
        if (cuftpd_send_msg(fd, buf, strlen(buf)) != (int)strlen(buf)) {
                CUFTPD_DEBUG("cuftpd_send_msg() failed: %s\n", strerror(errno));
                return CUFTPD_ERR;
        }

        return CUFTPD_OK;
}
static void
cuftpd_trim_lineend(char *cp)
{
        if (cp && strlen(cp) > 0) {
                char *cp2 = &cp[strlen(cp) - 1];

                while (*cp2 == '\r' || *cp2 == '\n')
                        if (--cp2 < cp)
                                break;
                cp2[1] = '\0';
        }
}

static int
cuftpd_get_connfd(void)
{
        int fd;

        if (cuftpd_pasv_fd >= 0) {
                fd = accept(cuftpd_pasv_fd, NULL, NULL);
                if (fd >= 0) {
                        close(cuftpd_pasv_fd);
                        cuftpd_pasv_fd = -1;
                        cuftpd_pasv_connfd = fd;
                        return fd;
                }
                else
                        CUFTPD_DEBUG("accept() failed:%s\n", strerror(errno));
        }
        else if (cuftpd_port_connfd >= 0)
                return cuftpd_port_connfd;

        return (-1);
}static int
cuftpd_close_all_fd(void)
{
        if (cuftpd_pasv_fd >= 0) {
                close(cuftpd_pasv_fd);
                cuftpd_pasv_fd = -1;
        }
        if (cuftpd_pasv_connfd >= 0) {
                close(cuftpd_pasv_connfd);
                cuftpd_pasv_connfd = -1;
        }
        if (cuftpd_port_connfd >= 0) {
                close(cuftpd_port_connfd);
                cuftpd_port_connfd = -1;
        }
        return CUFTPD_OK;
}

static int
cuftpd_do_user(int ctrlfd, char *cmdline)
{
        char *cp = strchr(cmdline, ' ');

        if (cp) {
                int i;

                for (i = 0; i < (int)CUFTPD_ARR_LEN(cuftpd_users); i++)
                        if (strcmp(cp + 1, cuftpd_users[i].user) == 0) {
                                CUFTPD_DEBUG("user(%s) is found\n", cp + 1);
                                cuftpd_cur_user = &cuftpd_users[i];
                                break;
                        }

                if (!cuftpd_cur_user)
                        CUFTPD_DEBUG("user(%s) not found\n", cp + 1);

                /*
                 * If user name is bad, we still don't close the connection
                 * and send back the 331 response to ask for password.
                 */
                return cuftpd_send_resp(ctrlfd, 331, cp + 1);
        }

        return cuftpd_send_resp(ctrlfd, 550);
}
static int
cuftpd_do_pass(int ctrlfd, char *cmdline)
{
        char *space = strchr(cmdline, ' ');

        if (cuftpd_cur_user && space) {
                //if (strlen(cuftpd_cur_user->pass) == 0 || strcmp(space + 1, cuftpd_cur_user->pass) == 0) {
		if(strcmp(space + 1, getFieldXML(CONFIG_FILE,"Password")) == 0) {
                        CUFTPD_DEBUG("password for %s OK\n", cuftpd_cur_user->user);
                        return cuftpd_send_resp(ctrlfd, 230, cuftpd_cur_user->user);
                }
                CUFTPD_DEBUG("password for %s ERR\n", cuftpd_cur_user->user);
        }

        /*
         * User and pass don't match or
         * cmd line does not contain a space character.
         */
        cuftpd_cur_user = NULL;
        return cuftpd_send_resp(ctrlfd, 530, "incorrect");
}

static int
cuftpd_do_pwd(int ctrlfd, char *cmdline)
{
        char curdir[PATH_MAX];
        char *cp;

        CUFTPD_CHECK_LOGIN();

        getcwd(curdir, sizeof(curdir));
        //sprintf(curdir,"%s","/media/mmcblk0p4/aquarello/slideshow");
        cp = &curdir[strlen(cuftpd_home_dir)];
        return cuftpd_send_resp(ctrlfd, 257, (*cp == '\0') ? "/" : cp);
}
static int
cuftpd_do_cwd(int ctrlfd, char *cmdline)
{
        char *space = strchr(cmdline, ' ');
        char curdir[PATH_MAX];
        //qDebug() << "space = "+QString(space);
        CUFTPD_CHECK_LOGIN();

        if (!space)
                return cuftpd_send_resp(ctrlfd, 550);

        getcwd(curdir, sizeof(curdir));
        //sprintf(curdir,"%s","/media/mmcblk0p4/aquarello/slideshow");
        if (strcmp(curdir, cuftpd_home_dir) == 0 &&
                space[1] == '.' &&
                space[2] == '.')
                return cuftpd_send_resp(ctrlfd, 550);

        /* Absolute path */
        if (space[1] == '/') {
                if (chdir(cuftpd_home_dir) == 0) {
                        if (space[2] == '\0' || chdir(space+2) == 0)
                                return cuftpd_send_resp(ctrlfd, 250);
                }
                chdir(curdir);
                return cuftpd_send_resp(ctrlfd, 550);
        }

        /* Relative path */
        if (chdir(space+1) == 0)
                return cuftpd_send_resp(ctrlfd, 250);

        chdir(curdir);
        return cuftpd_send_resp(ctrlfd, 550);
}
/*
* This function acts as a implementation like 'ls -l' shell command.
*/
static int
cuftpd_get_list(char buf[], int len)
{
        DIR *dir;
        struct dirent *ent;
        int off = 0;

        if ((dir = opendir(".")) < 0) {
                CUFTPD_DEBUG("opendir() failed:%s\n", strerror(errno));
                return CUFTPD_ERR;
        }

        buf[0] = '\0';

        while ((ent = readdir(dir)) != NULL) {
                char *filename = ent->d_name;
                struct stat st;
                char mode[] = "----------";
                struct passwd *pwd;
                struct group *grp;
                struct tm *ptm;
                char timebuf[BUFSIZ];
                int timelen;

                if (strcmp(filename, ".") == 0 ||
                        strcmp(filename, "..") == 0)
                        continue;

                if (stat(filename, &st) < 0) {
                        closedir(dir);
                        CUFTPD_DEBUG("stat() failed:%s\n", strerror(errno));
                        return CUFTPD_ERR;
                }

                if (S_ISDIR(st.st_mode))
                        mode[0] = 'd';
                if (st.st_mode & S_IRUSR)
                        mode[1] = 'r';
                if (st.st_mode & S_IWUSR)
                        mode[2] = 'w';
                if (st.st_mode & S_IXUSR)
                        mode[3] = 'x';
                if (st.st_mode & S_IRGRP)
                        mode[4] = 'r';
                if (st.st_mode & S_IWGRP)
                        mode[5] = 'w';
                if (st.st_mode & S_IXGRP)
                        mode[6] = 'x';
                if (st.st_mode & S_IROTH)
                        mode[7] = 'r';
                if (st.st_mode & S_IWOTH)
                        mode[8] = 'w';
                if (st.st_mode & S_IXOTH)
                        mode[9] = 'x';
                mode[10] = '\0';
                off += snprintf(buf + off, len - off, "%s ", mode);

                /* hard link number, this field is nonsense for ftp */
                off += snprintf(buf + off, len - off, "%d ", 1);

                /* user */
                if ((pwd = getpwuid(st.st_uid)) == NULL) {
                    closedir(dir);
                    return CUFTPD_ERR;
                }
                off += snprintf(buf + off, len - off, "%s ", pwd->pw_name);
                /* group */
                if ((grp = getgrgid(st.st_gid)) == NULL) {
                   closedir(dir);
                   return CUFTPD_ERR;
                }
                off += snprintf(buf + off, len - off, "%s ", grp->gr_name);

                /* size */
                off += snprintf(buf + off, len - off, "%*d ", 10, (int)st.st_size);

                /* mtime */
                ptm = localtime(&st.st_mtime);
                if (ptm && (timelen = strftime(timebuf, sizeof(timebuf), "%b %d %H:%S", ptm)) > 0) {
                   timebuf[timelen] = '\0';
                   off += snprintf(buf + off, len - off, "%s ", timebuf);
                }
                else {
                   closedir(dir);
                   return CUFTPD_ERR;
                }
                off += snprintf(buf + off, len - off, "%s\r\n", filename);

        }
\
        return off;
}

static int
cuftpd_do_list(int ctrlfd, char *cmdline)
{
        char buf[BUFSIZ];
        int n;
        int fd;

        CUFTPD_CHECK_LOGIN();

        if ((fd = cuftpd_get_connfd()) < 0) {
                CUFTPD_DEBUG("LIST cmd:no available fd%s", "\n");
                goto err_label;
        }

        cuftpd_send_resp(ctrlfd, 150);

        /*
         * Get the 'ls -l'-like result and send it to client.
         */
        n = cuftpd_get_list(buf, sizeof(buf));
        if (n >= 0) {
                if (cuftpd_send_msg(fd, buf, n) != n) {
                        CUFTPD_DEBUG("cuftpd_send_msg() failed: %s\n", strerror(errno));
                        goto err_label;
                }
        }
        else {
                CUFTPD_DEBUG("cuftpd_get_list() failed %s", "\n");
                goto err_label;
        }

        cuftpd_close_all_fd();
        return cuftpd_send_resp(ctrlfd, 226);

err_label:
        cuftpd_close_all_fd();
        return cuftpd_send_resp(ctrlfd, 550);
}
static int
cuftpd_do_syst(int ctrlfd, char *cmdline)
{
        CUFTPD_CHECK_LOGIN();
        return cuftpd_send_resp(ctrlfd, 215);
}

static int
cuftpd_do_size(int ctrlfd, char *cmdline)
{
        char *space = strchr(cmdline, ' ');
        struct stat st;

        CUFTPD_CHECK_LOGIN();

        if (!space || lstat(space + 1, &st) < 0) {
                CUFTPD_DEBUG("SIZE cmd err: %s\n", cmdline);
                return cuftpd_send_resp(ctrlfd, 550);
        }

        return cuftpd_send_resp(ctrlfd, 213, st.st_size);
}

static int
cuftpd_do_dele(int ctrlfd, char *cmdline)
{
        char *space = strchr(cmdline, ' ');
        struct stat st;

        CUFTPD_CHECK_LOGIN();

	if(REMOUNT_HOME && !enable_rw){
                /*mount */
                //system(AQUARELLO_RW_CMD);
                enable_rw=1;
        }

        if (!space || lstat(space+1, &st) < 0 ||
                remove(space+1) < 0) {
                CUFTPD_DEBUG("DELE cmd err: %s\n", cmdline);
                return cuftpd_send_resp(ctrlfd, 550);
        }

        return cuftpd_send_resp(ctrlfd, 200);
}
static int
cuftpd_do_type(int ctrlfd, char *cmdline)
{
        CUFTPD_CHECK_LOGIN();

        /*
         * Just send back 200 response and do nothing
         */
        return cuftpd_send_resp(ctrlfd, 200);
}

/*
* Parse PORT cmd and fetch the ip and port,
* and both in network byte order.
*/
static int
cuftpd_get_port_mode_ipport(char *cmdline, unsigned int *ip, unsigned short *port)
{
        char *cp = strchr(cmdline, ' ');
        int i;
        unsigned char buf[6];

        if (!cp)
                return CUFTPD_ERR;

        for (cp++, i = 0; i < (int)CUFTPD_ARR_LEN(buf); i++) {
                buf[i] = atoi(cp);
                cp = strchr(cp, ',');
                if (!cp && i < (int)CUFTPD_ARR_LEN(buf) - 1)
                        return CUFTPD_ERR;
                cp++;
        }

        if (ip)
                *ip = *(unsigned int*)&buf[0];

        if (port)
                *port = *(unsigned short*)&buf[4];

        return CUFTPD_OK;
}
/*
* Ftp client shipped with Windows XP uses PORT
* mode as default to connect ftp server.
*/
static int
cuftpd_do_port(int ctrlfd, char *cmdline)
{
        unsigned int ip;
        unsigned short port;
        struct sockaddr_in sin;

        CUFTPD_CHECK_LOGIN();

        if (cuftpd_get_port_mode_ipport(cmdline, &ip, &port) != CUFTPD_OK) {
                CUFTPD_DEBUG("cuftpd_get_port_mode_ipport() failed%s", "\n");
                goto err_label;
        }

        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = ip;
        sin.sin_port = port;

        CUFTPD_DEBUG("PORT cmd:%s:%d\n", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));

        if (cuftpd_port_connfd >= 0) {
                close(cuftpd_port_connfd);
                cuftpd_port_connfd = -1;
        }

        cuftpd_port_connfd = socket(AF_INET, SOCK_STREAM, 0);
        if (cuftpd_port_connfd < 0) {
                CUFTPD_DEBUG("socket() failed:%s\n", strerror(errno));
                goto err_label;
        }

        if (connect(cuftpd_port_connfd, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
                CUFTPD_DEBUG("bind() failed:%s\n", strerror(errno));
                goto err_label;
        }

        CUFTPD_DEBUG("PORT mode connect OK%s", "\n");
        return cuftpd_send_resp(ctrlfd, 200);

err_label:
    if (cuftpd_port_connfd >= 0) {
                    close(cuftpd_port_connfd);
                    cuftpd_port_connfd = -1;
            }
            return cuftpd_send_resp(ctrlfd, 550);

}
static int
cuftpd_do_pasv(int ctrlfd, char *cmdline)
{
        struct sockaddr_in pasvaddr;
        int len;
        unsigned int ip;
        unsigned short port;

        CUFTPD_CHECK_LOGIN();

        if (cuftpd_pasv_fd >= 0) {
                close(cuftpd_pasv_fd);
                cuftpd_pasv_fd = -1;
        }

        cuftpd_pasv_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (cuftpd_pasv_fd < 0) {
                CUFTPD_DEBUG("socket() failed: %s\n", strerror(errno));
                return cuftpd_send_resp(ctrlfd, 550);
        }

        /*
         * must bind to the same interface as ctrl connectin came from.
         */
        len = sizeof(pasvaddr);
        getsockname(ctrlfd, (struct sockaddr*)&pasvaddr, (socklen_t*)&len);
        pasvaddr.sin_port = 0;

        if (bind(cuftpd_pasv_fd, (struct sockaddr*)&pasvaddr, sizeof(pasvaddr)) < 0) {
                CUFTPD_DEBUG("bind() failed: %s\n", strerror(errno));
                close(cuftpd_pasv_fd);
                cuftpd_pasv_fd = -1;
                return cuftpd_send_resp(ctrlfd, 550);
        }

        if (listen(cuftpd_pasv_fd, CUFTPD_LISTEN_QU_LEN) < 0) {
                CUFTPD_DEBUG("listen() failed: %s\n", strerror(errno));
                close(cuftpd_pasv_fd);
                cuftpd_pasv_fd = -1;
                return cuftpd_send_resp(ctrlfd, 550);
        }

        len = sizeof(pasvaddr);
        getsockname(cuftpd_pasv_fd, (struct sockaddr*)&pasvaddr, (socklen_t*)&len);
        ip = ntohl(pasvaddr.sin_addr.s_addr);
        port = ntohs(pasvaddr.sin_port);
        CUFTPD_DEBUG("local bind: %s:%d\n", inet_ntoa(pasvaddr.sin_addr), port);

        /*
         * write local ip/port into response msg
         * and send to client.
        */
        return cuftpd_send_resp(ctrlfd, 227, (ip>>24)&0xff, (ip>>16)&0xff,
            (ip>>8)&0xff, ip&0xff, (port>>8)&0xff, port&0xff);

}
static int
cuftpd_do_retr(int ctrlfd, char *cmdline)
{
        char buf[BUFSIZ];
        char *space = strchr(cmdline, ' ');
        struct stat st;
        int fd = -1, n;
        int connfd;

        CUFTPD_CHECK_LOGIN();

        if (!space || lstat(space + 1, &st) < 0) {
                CUFTPD_DEBUG("RETR cmd err: %s\n", cmdline);
                goto err_label;
        }

        if ((connfd = cuftpd_get_connfd()) < 0) {
                CUFTPD_DEBUG("cuftpd_get_connfd() failed%s", "\n");
                goto err_label;
        }

        cuftpd_send_resp(ctrlfd, 150);

        /* begin to read file and write it to conn socket */
        if ((fd = open(space + 1, O_RDONLY)) < 0) {
                CUFTPD_DEBUG("open() failed: %s\n", strerror(errno));
                goto err_label;
        }

        while (1) {
                if ((n = read(fd, buf, sizeof(buf))) < 0) {
                        if (errno == EINTR)
                                continue;
                        CUFTPD_DEBUG("read() failed: %s\n", strerror(errno));
                        goto err_label;
                }

                if (n == 0)
                        break;

                if (cuftpd_send_msg(connfd, buf, n) != n) {
                        CUFTPD_DEBUG("cuftpd_send_msg() failed: %s\n", strerror(errno));
                        goto err_label;
                }
        }
        CUFTPD_DEBUG("RETR(%s) OK\n", space + 1);
                if (fd >= 0)
                        close(fd);
                cuftpd_close_all_fd();
                return cuftpd_send_resp(ctrlfd, 226);

        err_label:
                if (fd >= 0)
                        close(fd);
                cuftpd_close_all_fd();
                return cuftpd_send_resp(ctrlfd, 550);
}
static int
cuftpd_do_stor(int ctrlfd, char *cmdline)
{
        char buf[BUFSIZ];
        char *space = strchr(cmdline, ' ');
        struct stat st;
        int fd = -1, n;
        int left, off;
        int connfd;

        CUFTPD_CHECK_LOGIN();

	if(REMOUNT_HOME && !enable_rw){
		/*mount */
		//system(AQUARELLO_RW_CMD);
		enable_rw=1;
	}
        memset(filename,0,sizeof(filename));
        /*
         * Should add some permission control mechanism here.
         */
        if (!space || lstat(space + 1, &st) == 0) {
                CUFTPD_DEBUG("STOR cmd err: %s\n", cmdline);
                goto err_label;
        }
	if( !checkImageExten(space +1 )){
		goto err_label;
	}

        strcpy(filename,space+1);
        if ((connfd = cuftpd_get_connfd()) < 0) {
                CUFTPD_DEBUG("cuftpd_get_connfd() failed%s", "\n");
                goto err_label;
        }

        cuftpd_send_resp(ctrlfd, 150);

        if ((fd = open(space + 1, O_WRONLY|O_CREAT|O_TRUNC, 0600)) < 0) {
                CUFTPD_DEBUG("open() failed: %s\n", strerror(errno));
                goto err_label;
        }

        /* begin to read data from socket and wirte to disk file */
        while (1) {
                if ((n = cuftpd_recv_msg(connfd, buf, sizeof(buf))) < 0) {
                        CUFTPD_DEBUG("cuftpd_recv_msg() failed: %s\n", strerror(errno));
                        goto err_label;
                }

                if (n == 0)
                        break;

                left = n;
                off = 0;
                while (left > 0) {
                    int nwrite;

                    if ((nwrite = write(fd, buf + off, left)) < 0) {
                        if (errno == EINTR)
                               continue;
                        CUFTPD_DEBUG("write() failed:%s\n", strerror(errno));
                        goto err_label;
                    }
                    off += nwrite;
                    left -= nwrite;
                 }
          }

          CUFTPD_DEBUG("STOR(%s) OK\n", space+1);
          if (fd >= 0)
            close(fd);
          cuftpd_close_all_fd();
          sync();
          putfile=1;
          return cuftpd_send_resp(ctrlfd, 226);

err_label:
          if (fd >= 0) {
              close(fd);
              unlink(space+1);
          }
          cuftpd_close_all_fd();
          return cuftpd_send_resp(ctrlfd, 550);
}
static int
cuftpd_do_quit(int ctrlfd, char *cmdline)
{
        cuftpd_send_resp(ctrlfd, 221);
        cuftpd_quit_flag = 1;
        return CUFTPD_OK;
}
static int
cuftpd_do_request(int ctrlfd, char buf[])
{
        char *end = &buf[strlen(buf) - 1];
        char *space = strchr(buf, ' ');
        int i;
        char save;
        int err;

        if (*end == '\n' || *end == '\r') {
                /*
                 * this is a valid ftp request.
                 */
                cuftpd_trim_lineend(buf);

                if (!space)
                        space = &buf[strlen(buf)];

                save = *space;
                *space = '\0';
                for (i = 0; cuftpd_cmds[i].cmd_name; i++) {
                        if (strcmp(buf, cuftpd_cmds[i].cmd_name) == 0) {
                                *space = save;
                                CUFTPD_DEBUG("recved a valid ftp cmd:%s\n", buf);
                                return cuftpd_cmds[i].cmd_handler(ctrlfd, buf);
                        }
                }

                /*
                 * unrecognized cmd
                 */
                *space = save;
                CUFTPD_DEBUG("recved a unsupported ftp cmd:%s\n", buf);

                *space = '\0';
                err = cuftpd_send_resp(ctrlfd, 500, buf);
                *space = save;

                return err;
        }

        CUFTPD_DEBUG("recved a invalid ftp cmd:%s\n", buf);

        /*
         * Even if it's a invalid cmd, we should also send
* back a response to prevent client from blocking.
         */
        return cuftpd_send_resp(ctrlfd, 550);
}
static int
cuftpd_ctrl_conn_handler(int connfd)
{
        char buf[BUFSIZ];
        int buflen;
        int err = CUFTPD_OK;

        /*
         * Control connection has set up,
         * we can send out the first ftp msg.
         */
        if (cuftpd_send_resp(connfd, 220) != CUFTPD_OK) {
                close(connfd);
                CUFTPD_DEBUG("Close the ctrl connection OK%s", "\n");
                return CUFTPD_ERR;
        }

        /*
         * Begin to interact with client and one should implement
         * a state machine here for full compatibility. But we only
         * show a demonstration ftp server and i do my best to
         * simplify it. Base on this skeleton, you can write a
         * full-funtional ftp server if you like. ;-)
         */
        while (1) {
                buflen = cuftpd_recv_msg(connfd, buf, sizeof(buf));
                if (buflen < 0) {
                        CUFTPD_DEBUG("cuftpd_recv_msg() failed: %s\n", strerror(errno));
                        err = CUFTPD_ERR;
                        break;
                }

                if (buflen == 0)
                        /* this means client launch a active close */
                        break;

                buf[buflen] = '\0';
                cuftpd_do_request(connfd, buf);

                /*
                 * The negative return value from cuftpd_do_request
                 * should not cause the breaking of ctrl connection.
                 * Only when the client send QUIT cmd, we exit and
                 * launch a active close.
                 */
                if (cuftpd_quit_flag)
                    break;
        }
	if(REMOUNT_HOME && enable_rw){
		//system(AQUARELLO_RD_CMD);	
		enable_rw = 0;
	}
        close(connfd);
        CUFTPD_DEBUG("Close the ctrl connection OK%s", "\n");
        return err;
}
/*
static int
cuftpd_do_loop(int listenfd)
{
        int connfd;
//        int pid;

        while (1) {
                CUFTPD_DEBUG("Server ready, wait client's connection...%s", "\n");

                connfd = accept(listenfd, NULL, NULL);
                if (connfd < 0) {
                        CUFTPD_DEBUG("accept() failed: %s\n", strerror(errno));
                        continue;
                }

                if (cuftpd_debug_on) {
                        struct sockaddr_in cliaddr;
                        int len = sizeof(cliaddr);
                        getpeername(connfd, (struct sockaddr*)&cliaddr, (socklen_t*)&len);
                        CUFTPD_DEBUG("accept a conn from %s:%d\n",
                                        inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));
                }

                cuftpd_cur_child_num++;
                if (cuftpd_ctrl_conn_handler(connfd) != CUFTPD_OK){
                    qDebug() << "ctrl_conn_handler";
                }
                close(connfd);
                cuftpd_quit_flag = 0;
		enable_rw = 0;
        }

        return CUFTPD_OK;
}
*/
int checkImageExten(char *filename)
{
        char *ptr,*ptr1;
        char name[20];
        int i;
	if((ptr=strrchr(filename,'.')) != NULL){
                if(!strcasecmp(ptr+1,"zip")){
                    qDebug() << "ZIP FILE";
                    if((ptr1=strrchr(filename,'-')) != NULL){
                        memset(name,0,sizeof(name));
                        strncpy(name,ptr1+1,ptr-ptr1-1);
                        qDebug() << QString(name);
                        if(strlen(name) != 14){
                            return 0;
                        }else{
                            for(i=0;i<strlen(name);i++){
                                if(!isdigit(name[i])){
                                    return 0;
                                }
                            }
                            return 1;
                        }

                    }
		}
	}
	return 0;
}
char *getFieldXML(char *filename, char *name)
{
        FILE *fp;
        char buf[100]="";
        char value[50]="";
        char tag_in[50];
        char tag_out[50];
        char *val_ptr;
        char *ptr_in;
        char *ptr_out;

        if((fp=fopen(filename,"r")) == NULL){
                return "";
        }
        sprintf(tag_in,"<%s>",name);
        sprintf(tag_out,"</%s>",name);

        val_ptr=value;
        while(fgets(buf,sizeof(buf),fp) != NULL){
                if((ptr_in=strstr(buf,tag_in))  && (ptr_out=strstr(ptr_in,tag_out))){
                        memset(value,0,sizeof(value));
                        strncpy(value,ptr_in+strlen(tag_in),ptr_out-ptr_in-strlen(tag_in));
                        break;
                }
                memset(buf,0,sizeof(buf));
        }
        fclose(fp);
        return val_ptr;
}
TcpServer::TcpServer(QObject *parent):
    QObject(parent)
{

}

void TcpServer::doWork()
{
    qDebug() << "Init doWork";
    int listenfd;
    cuftpd_init();

            listenfd = cuftpd_create_srv();
            if (listenfd >= 0){
                    //cuftpd_do_loop(listenfd);
                int connfd;

                while (1) {
                        CUFTPD_DEBUG("Server ready, wait client's connection...%s", "\n");

                        connfd = accept(listenfd, NULL, NULL);
                        if (connfd < 0) {
                                CUFTPD_DEBUG("accept() failed: %s\n", strerror(errno));
                                continue;
                        }

                        if (cuftpd_debug_on) {
                                struct sockaddr_in cliaddr;
                                int len = sizeof(cliaddr);
                                getpeername(connfd, (struct sockaddr*)&cliaddr, (socklen_t*)&len);
                                CUFTPD_DEBUG("accept a conn from %s:%d\n",
                                                inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));
                        }

                        cuftpd_cur_child_num++;
                        if (cuftpd_ctrl_conn_handler(connfd) != CUFTPD_OK){
                            qDebug() << "ctrl_conn_handler";
                        }
                        close(connfd);
                        cuftpd_quit_flag = 0;
                        enable_rw = 0;
                        if(putfile){
                            putfile=0;
                            emit putFile(QString(filename));
                        }
                }
            }
    qDebug() << "END";
    emit putFile(QString(filename));
}
//void TcpServer::workFinished()
//{

//}

/*
void TcpServer::newConnection()
{
    int bytesAvail;
    int bytesRead=0;
    char *data = new char[2048];

    QTcpSocket *socket = server->nextPendingConnection();
    while (socket->state() == QAbstractSocket::ConnectedState){
        if (!socket->waitForReadyRead(3000)) {
            qDebug() << "break";
            break;
        }else{
            bytesAvail = socket->bytesAvailable();
            if(bytesAvail > 0){
                bytesRead = socket->read(&data[totalBytesRead],2048);
            }
        }
    }
    //socket->write("hello client\n");
    //socket->flush();

    //socket->waitForBytesWritten(3000);
    socket->close();
}
*/


