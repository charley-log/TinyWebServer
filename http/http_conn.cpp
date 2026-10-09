
#include "http_conn.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

// === 静态成员定义 ===
UserCache http_conn::users;
Epoller *http_conn::m_epoller = nullptr;
std::atomic<int> http_conn::m_user_count(0);

const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";

void http_conn::initmysql_result(connection_pool *connPool)
{
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    const char table[] = "user";
    const char sql[] = "SELECT username, passwd FROM user;";

    if (mysql_query(mysql, sql) != 0)
    {
        printf("query error\n");
        return;
    }

    MYSQL_RES *result = mysql_store_result(mysql);
    int num_fields = mysql_num_fields(result);
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != NULL)
    {
        string username = row[0];
        string password = row[1];
        users.insert(username, password);
    }

    mysql_free_result(result);
}

const char *http_conn::get_content_type(const char *path)
{
    if (!path)
        return "application/octet-stream";

    const char *ext = strrchr(path, '.');
    if (!ext)
        return "application/octet-stream";

    if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0)
        return "text/html";
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)
        return "image/jpeg";
    if (strcasecmp(ext, ".png") == 0)
        return "image/png";
    if (strcasecmp(ext, ".gif") == 0)
        return "image/gif";
    if (strcasecmp(ext, ".css") == 0)
        return "text/css";
    if (strcasecmp(ext, ".js") == 0)
        return "application/javascript";
    if (strcasecmp(ext, ".avi") == 0)
        return "video/x-msvideo";
    if (strcasecmp(ext, ".mp4") == 0)
        return "video/mp4";

    return "application/octet-stream";
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = nullptr;

    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
           ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();
        m_start_line = m_checked_idx;

        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        case CHECK_STATE_HEADER:
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            if (ret == GET_REQUEST)
                return do_request();
            break;
        case CHECK_STATE_CONTENT:
            ret = parse_content(text);
            if (ret == GET_REQUEST)
                return do_request();
            line_status = LINE_OPEN;
            break;
        default:
            return INTERNAL_ERROR;
        }
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    const char *p = strrchr(m_url, '/');
    if (!p)
        return BAD_REQUEST;

    char code = *(p + 1);

    if (code == '0')
    {
        strcpy(m_real_file, m_root);
        strcat(m_real_file, "/register.html");
    }
    else if (code == '1')
    {
        strcpy(m_real_file, m_root);
        strcat(m_real_file, "/log.html");
    }
    else if (code == '2') // 登录
    {
        if (m_method != POST)
            return BAD_REQUEST;

        // 解析 user / password（保留你原来的逻辑）
        const char *user_prefix = "user=";
        const char *passwd_prefix = "password=";
        const char *user_val = strstr(m_read_buf + m_start_line, user_prefix);
        const char *passwd_val = strstr(m_read_buf + m_start_line, passwd_prefix);
        if (!user_val || !passwd_val)
            return BAD_REQUEST;

        user_val += strlen(user_prefix);
        passwd_val += strlen(passwd_prefix);

        string user(user_val);
        string passwd(passwd_val);
        size_t pos;
        if ((pos = user.find('&')) != string::npos)
            user.resize(pos);
        if ((pos = passwd.find('&')) != string::npos)
            passwd.resize(pos);
        if ((pos = passwd.find('\r')) != string::npos)
            passwd.resize(pos);
        if ((pos = passwd.find('\n')) != string::npos)
            passwd.resize(pos);

        string stored;
        if (!users.find(user, stored) || stored != passwd)
        {
            // 登录失败 → 回到登录页
            strcpy(m_real_file, m_root);
            strcat(m_real_file, "/log.html");
        }
        else
        {
            // 登录成功 → 欢迎页
            strcpy(m_real_file, m_root);
            strcat(m_real_file, "/welcome.html");
        }
        // ★ 注意：这里不要再 return，让代码落到下面的公共文件服务部分
    }
    else if (code == '3') // 注册
    {
        if (m_method != POST)
            return BAD_REQUEST;

        const char *user_prefix = "user=";
        const char *passwd_prefix = "password=";
        const char *user_val = strstr(m_read_buf + m_start_line, user_prefix);
        const char *passwd_val = strstr(m_read_buf + m_start_line, passwd_prefix);
        if (!user_val || !passwd_val)
            return BAD_REQUEST;

        user_val += strlen(user_prefix);
        passwd_val += strlen(passwd_prefix);

        string user(user_val);
        string passwd(passwd_val);
        size_t pos;
        if ((pos = user.find('&')) != string::npos)
            user.resize(pos);
        if ((pos = passwd.find('&')) != string::npos)
            passwd.resize(pos);
        if ((pos = passwd.find('\r')) != string::npos)
            passwd.resize(pos);
        if ((pos = passwd.find('\n')) != string::npos)
            passwd.resize(pos);

        if (!users.try_insert(user, passwd))
        {
            // 用户名已存在 → 回到注册页
            strcpy(m_real_file, m_root);
            strcat(m_real_file, "/register.html");
        }
        else
        {
            // 注册成功 → 去登录页
            strcpy(m_real_file, m_root);
            strcat(m_real_file, "/log.html");
        }
    }
    else if (code == '5')
    {
        strcpy(m_real_file, m_root);
        strcat(m_real_file, "/picture.html");
    }
    else if (code == '6')
    {
        strcpy(m_real_file, m_root);
        strcat(m_real_file, "/video.html");
    }
    else if (code == '7')
    {
        strcpy(m_real_file, m_root);
        strcat(m_real_file, "/fans.html");
    }
    else
    {
        if (strlen(m_url) >= 2 && m_url[0] == '/')
        {
            if (strstr(m_url, "..") || strstr(m_url, "//"))
                return NO_RESOURCE;
            if (strlen(m_root) + strlen(m_url) + 1 >= FILENAME_LEN)
                return NO_RESOURCE;
            strcpy(m_real_file, m_root);
            strcat(m_real_file, m_url);
        }
        else
        {
            return NO_RESOURCE;
        }
    }

    // ============ 公共文件服务 ============
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;

    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    m_file_fd = open(m_real_file, O_RDONLY);
    if (m_file_fd < 0)
        return NO_RESOURCE;

    // ★ 补上 mmap（你之前 wrk/curl 能返回 200 就是靠这段）
    if (m_file_stat.st_size > 0)
    {
        m_file_address = (char *)mmap(0, m_file_stat.st_size,
                                      PROT_READ, MAP_PRIVATE, m_file_fd, 0);
        if (m_file_address == MAP_FAILED)
        {
            m_file_address = nullptr;
            close(m_file_fd);
            m_file_fd = -1;
            return INTERNAL_ERROR;
        }
    }
    close(m_file_fd);
    m_file_fd = -1;

    return FILE_REQUEST;
}

void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = nullptr;
    }
}

bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;

    case BAD_REQUEST:
        add_status_line(400, error_400_title);
        add_headers(strlen(error_400_form));
        if (!add_content(error_400_form))
            return false;
        break;

    case NO_RESOURCE:
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;

    case FORBIDDEN_REQUEST:
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;

    case FILE_REQUEST:
        add_status_line(200, ok_200_title);
        add_content_type(get_content_type(m_real_file));
        add_headers((int)m_file_stat.st_size);

        if (m_file_stat.st_size != 0)
        {
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            m_bytes_to_send = m_write_idx + m_file_stat.st_size;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv_count = 1;
            m_bytes_to_send = m_write_idx;
        }
        return true; // 文件响应自己设好 iov 了

    default:
        return false;
    }

    // ★ 错误响应统一走这里：只有 write_buf 一段
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    m_bytes_to_send = m_write_idx;
    return true;
}

void http_conn::process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        resetEpoll(true);  // 监听可读
        return;
    }

    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
        return;
    }

    resetEpoll(false); // 监听可写
}

bool http_conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;

    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - m_write_idx, format, arg_list);
    if (len >= WRITE_BUFFER_SIZE - m_write_idx)
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("HTTP/1.1 %d %s\r\n", status, title);
}

bool http_conn::add_content_type(const char *type)
{
    if (!type)
        return false;
    return add_response("Content-Type: %s\r\n", type);
}

bool http_conn::add_content_length(int len)
{
    return add_response("Content-Length: %d\r\n", len);
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", m_linger ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
    return true;
}

bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

ssize_t http_conn::writev(int fd, const struct iovec *iov, int iovcnt)
{
    ssize_t ret;
    do
    {
        ret = ::writev(fd, iov, iovcnt);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

bool http_conn::write()
{
    if (m_bytes_to_send == 0)
    {
        resetEpoll(true);  // 监听可读
        if (m_linger)
            init();
        return true;
    }

    while (true)
    {
        ssize_t temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp < 0)
        {
            if (errno == EAGAIN)
            {
                resetEpoll(false); // 监听可写
                return true;
            }
            unmap();
            return false;
        }

        m_bytes_have_write += temp;
        m_bytes_to_send -= temp;

        if (m_bytes_to_send <= 0)
        {
            unmap();
            resetEpoll(true);  // 监听可读
            if (m_linger)
            {
                init();
                return true;
            }
            return false;
        }

        // === 重新构造 iov ===
        if (m_bytes_have_write >= m_write_idx)
        {
            // header 已经全部发完，接下来只发 file
            m_iv[0].iov_base = m_write_buf; // 占位，长度设 0
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (m_bytes_have_write - m_write_idx);
            m_iv[1].iov_len = m_bytes_to_send;
        }
        else
        {
            // header 还没发完，只发 header 剩余部分
            m_iv[0].iov_base = m_write_buf + m_bytes_have_write;
            m_iv[0].iov_len = m_write_idx - m_bytes_have_write;
            m_iv[1].iov_base = m_file_address; // 占位
            m_iv[1].iov_len = 0;
        }
    }
}

// --- 私有 init()：重置所有成员变量 ---
void http_conn::init()
{
    m_sockfd = -1;
    memset(&m_address, 0, sizeof(m_address));
    memset(m_read_buf, 0, READ_BUFFER_SIZE);
    m_read_idx = 0;
    m_checked_idx = 0;
    m_start_line = 0;
    memset(m_write_buf, 0, WRITE_BUFFER_SIZE);
    m_write_idx = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_method = GET;
    memset(m_real_file, 0, FILENAME_LEN);
    m_url = nullptr;
    m_version = nullptr;
    m_host = nullptr;
    m_content_length = 0;
    m_linger = false;
    m_file_address = nullptr;
    memset(&m_file_stat, 0, sizeof(m_file_stat));
    m_file_fd = -1;
    m_iv_count = 0;
    cgi = 0;
    m_string = nullptr;
    m_bytes_to_send = 0;
    m_bytes_have_write = 0;
    m_root = nullptr;
    m_TRIGMode = 0;
    m_close_log = 0;
    memset(sql_user, 0, 100);
    memset(sql_passwd, 0, 100);
    memset(sql_name, 0, 100);
    timer_flag = 0;
    improv = 0;
    mysql = nullptr;
    m_state = 0;
}

// --- 公共 init(int, ...)：用外部参数初始化连接 ---
// [修复] 先调用私有init()重置所有成员，再设置具体值（避免m_sockfd被重置为-1）
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int trigmode, int close_log,
                     string user, string passwd, string sqlname)
{
    // 先重置所有成员变量
    init();

    // 再设置具体值
    m_sockfd = sockfd;
    m_address = addr;
    m_root = root;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    memset(sql_user, 0, 100);
    memset(sql_passwd, 0, 100);
    memset(sql_name, 0, 100);
    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    // [修复] 设置socket为非阻塞并加入epoll监听
    int old_option = fcntl(sockfd, F_GETFL);
    fcntl(sockfd, F_SETFL, old_option | O_NONBLOCK);

    uint32_t ev = Epoller::makeEvents(/*want_read=*/true, /*one_shot=*/false, trigmode);
    m_epoller->add(sockfd, ev);

    // [修复] 用户计数+1
    m_user_count++;
}

// --- parse_line：从读取缓冲区解析一行 ---
http_conn::LINE_STATUS http_conn::parse_line()
{
    if (m_checked_idx >= m_read_idx)
        return LINE_OPEN;

    // 循环读取直到找到完整的行结尾
    while (m_checked_idx < m_read_idx)
    {
        char temp = m_read_buf[m_checked_idx++];
        if (temp == '\r')
        {
            if (m_checked_idx >= m_read_idx)
                return LINE_OPEN;

            if (m_read_buf[m_checked_idx] == '\n')
            {
                // ★ 同时干掉 \r 和 \n，整行以 \0 结尾
                m_read_buf[m_checked_idx - 1] = '\0'; // 把 \r 覆盖为 \0
                m_read_buf[m_checked_idx] = '\0';     // 把 \n 覆盖为 \0（保险起见）
                m_checked_idx++;
                return LINE_OK;
            }
            return LINE_BAD;
        }
        if (temp == '\n')
        {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
                m_read_buf[m_checked_idx - 1] = '\0';
            m_read_buf[m_checked_idx++] = '\0';
            return LINE_OK;
        }
    }
    return LINE_OPEN;
}

// --- parse_request_line：解析请求行 "GET /path HTTP/1.1" ---
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // 1. 提取请求方法 (METHOD)
    char *method = strsep(&text, " \t");
    if (!method || !*method)
        return BAD_REQUEST;

    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
        m_method = POST;
    else if (strcasecmp(method, "HEAD") == 0)
        m_method = HEAD;
    else
        return BAD_REQUEST;

    // 2. 提取 URL
    m_url = strsep(&text, " \t");
    if (!m_url || !*m_url)
        return BAD_REQUEST;

    // 3. 提取版本号 (VERSION)
    m_version = strsep(&text, " \t");
    if (!m_version || !*m_version)
        return BAD_REQUEST;

    // 检查 HTTP 版本
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;

    // 检查 URL 长度
    if (strlen(m_url) > FILENAME_LEN)
        return BAD_REQUEST;

    // 默认首页路由处理
    if (strcmp(m_url, "/") == 0)
    {
        m_url = (char *)"/log.html"; // 将 "/" 重定向到登录页
    }

    m_check_state = CHECK_STATE_HEADER; // 状态机转移到解析请求头
    return NO_REQUEST;
}

// --- parse_headers：解析HTTP请求头 ---
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if (text[0] == '\0')
    {
        // 有 body 才进入 CONTENT 状态，否则本轮请求头就结束了
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST; // ★ 关键：让 process_read 去 do_request
    }

    if (strcasecmp(text, "Connection: keep-alive") == 0)
        m_linger = true;
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
        m_content_length = atol(text + 15);
    else if (strncasecmp(text, "Host:", 5) == 0)
        m_host = text + 5;

    return NO_REQUEST;
}

// --- parse_content：解析请求体 ---
http_conn::HTTP_CODE http_conn::parse_content(char *text) 
{
    // 按上一轮建议改
    long body_got = m_read_idx - m_start_line;
    if (body_got >= m_content_length)
        return GET_REQUEST;
    return NO_REQUEST;
}

// --- close_conn：关闭连接 ---
void http_conn::close_conn(bool real_close)
{
    unmap();
    if (m_sockfd != -1)
    {
        close(m_sockfd);
        m_sockfd = -1;
    }
    if (real_close)
    {
        m_user_count--;
    }
}

// --- read_once：从socket读取数据 ---
bool http_conn::read_once()
{
    // 缓冲区已经满了，不能再读
    if (m_read_idx >= READ_BUFFER_SIZE)
        return false;

    while (true)
    {
        ssize_t n = recv(m_sockfd,
                         m_read_buf + m_read_idx,
                         READ_BUFFER_SIZE - m_read_idx,
                         0);

        if (n < 0)
        {
            // ★ 关键：EAGAIN/EWOULDBLOCK 表示"本轮数据读完了"
            //   ET 模式下必须靠这个信号退出循环
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;

            // EINTR：被信号打断，重试
            if (errno == EINTR)
                continue;

            // 其它错误（ECONNRESET、EPIPE 等）：连接不可用
            return false;
        }

        if (n == 0)
        {
            // 对端主动关闭连接
            close_conn();
            return false;
        }

        // 成功读到 n 字节，累积到缓冲区
        m_read_idx += n;
        m_read_buf[m_read_idx] = '\0';

        // 缓冲区满则退出，留待下一次事件继续读
        if (m_read_idx >= READ_BUFFER_SIZE)
            break;
    }

    LOG_DEBUG("%s: %s", "get data from client:", m_read_buf);
    return true;
}

void http_conn::resetEpoll(bool want_read)
{
    uint32_t ev = Epoller::makeEvents(want_read, /*one_shot=*/true, m_TRIGMode);
    m_epoller->mod(m_sockfd, ev);
}