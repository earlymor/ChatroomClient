#include "TcpClient.h"
#include <stdlib.h>
#include <sys/fcntl.h>
#include <fstream>
#include <iostream>
#include <unordered_set>
#include "MyInput.h"
using namespace std;
// client初始化，socket,sem
TcpClient::TcpClient() {
    // 创建client端的socket
    m_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == m_fd) {
        std::cerr << "socket create error" << std::endl;
        exit(-1);
    }

    string emptystring;
    m_account = emptystring;
    vector<string> emptynotice;
    m_groupnotice = emptynotice;
    vector<string> emptyusernotice;
    m_usernotice = emptyusernotice;
    sem_init(&m_rwsem, 0, 0);
    m_friendmanager =
        new FriendManager(m_fd, m_rwsem, is_Friend, m_account, this);
    m_groupmanager = new GroupManager(m_fd, m_rwsem, is_Group, m_account, this);
    m_usermanager = new UserManager(m_fd, m_rwsem, m_account, this);
}

// 析构回收资源
TcpClient::~TcpClient() {
    close(m_fd);
    sem_destroy(&m_rwsem);
    delete m_friendmanager;
    delete m_groupmanager;
}

// 连接server -> 启动client
void TcpClient::connectServer(char* arg1, char* arg2) {
    // 解析通过命令行参数传递的ip和port
    char* ip = arg1;
    uint16_t port = atoi(arg2);
    // 填写client需要连接的server信息ip+port
    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);
    if (-1 == connect(m_fd, (sockaddr*)&server, sizeof(sockaddr_in))) {
        std::cerr << "connect server error" << std::endl;
        close(m_fd);
        exit(-1);
    }

    this->run();
}

// 启动client,启动读子线程
void TcpClient::run() {
    // 子线程读数据
    // 传入cfd参数用于读数据
    // 设置线程分离
    m_readTask = new thread(std::bind(&TcpClient::readTaskHandler, this, m_fd));
    m_readTask->detach();
    for (;;) {
        welcomeMenu();
        int choice = 0;
        bool legal = false;
        while (!legal) {
            cout << "请输入：";
            legal = dataInput(choice);
        }
        switch (choice) {
            case LOGIN: {
                handleLogin();
                break;
            }
            case REGISTER: {
                handleRegister();
                break;
            }
            case QUIT: {
                exit(0);
            }
            default:
                cerr << "invalid input!" << endl;
                break;
        }
    }
}

// 读任务处理器
void TcpClient::readTaskHandler(int cfd) {
    // 死循环接收消息
    for (;;) {
        char buffer[CLIENT_BUFSIZE] = {
            0};  // 默认1024字符的缓冲区，可能需要扩容

        int len = recv(cfd, buffer, CLIENT_BUFSIZE, 0);
        if (-1 == len || 0 == len) {
            cout << "error close cfd " << endl;
            close(cfd);
            sem_post(&m_rwsem);
            exit(-1);
        }

        // 接收ChatServer转发的数据，反序列化生成json数据对象
        try {
            json js = json::parse(buffer);
            int type = js["type"].get<int>();
            js.erase("type");  // 剔除type字段，只保留数据
            switch (type) {
                case LOGIN:
                    handleLoginResponse(js);
                    sem_post(&m_rwsem);  // Notify the main thread that login
                                         // response is handled
                    break;
                case REG:
                    handleRegisterResponse(js);
                    sem_post(&m_rwsem);  // Notify the main thread that register
                                         // response is handled
                    break;
                case FRIEND_GET_LIST:
                    handleFriendListResponse(js);
                    sem_post(&m_rwsem);
                    break;
                case GET_INFO:
                    sem_post(&m_rwsem);
                    break;
                case FRIEND_TYPE:
                    int friendtype;
                    friendtype = js["friendtype"].get<int>();
                    switch (friendtype) {
                        case FRIEND_ADD:
                            handleFriendAddResponse(js);
                            break;
                        case FRIEND_DELETE:
                            handleFriendDeleteResponse(js);
                            break;
                        case FRIEND_REQUIRY:
                            handleFriendRequiryResponse(js);
                            break;
                        case FRIEND_CHAT:
                            handleFriendChatResponse(js);
                            break;
                        case FRIEND_BLOCK:
                            handleFriendBlockResponse(js);
                            break;
                        case FRIEND_CHAT_REQUIRY:
                            is_Friend = false;
                            handleFriendChatRequiryResponse(js);
                    }
                    sem_post(&m_rwsem);
                    break;
                case GROUP_GET_LIST:
                    handleGroupListResponse(js);
                    sem_post(&m_rwsem);
                    break;
                case GROUP_TYPE:
                    int grouptype;
                    grouptype = js["grouptype"].get<int>();
                    switch (grouptype) {
                        case GROUP_ADD:
                            handleGroupAddResponse(js);
                            break;
                        case GROUP_CREATE:
                            handleGroupCreateResponse(js);
                            break;
                        case GROUP_ENTER:
                            handleGroupEnterResponse(js);
                            break;
                        case GROUP_REQUIRY:
                            handleGroupRequiryResponse(js);
                            break;
                        case GROUP_OWNER:
                            handleGroupOwnerResponse(js);
                            break;
                        case GROUP_ADMINISTRATOR:
                            handleGroupAdminResponse(js);
                            break;
                        case GROUP_MEMBER:
                            handleGroupMemberResponse(js);
                            break;
                        case GROUP_GET_NOTICE:
                            handleGroupGetNoticeResponse(js);
                            break;
                        default:
                            break;
                    }
                    sem_post(&m_rwsem);
                    break;
                case GROUP_SET_CHAT_STATUS:
                    handleSetChatAckResponse(js);
                    sem_post(&m_rwsem);
                    break;
                case GROUP_GET_LIST_LEN:
                    handleGetListLenResponse(js);
                    sem_post(&m_rwsem);
                    break;
                case GROUP_MSG:
                    handleGroupMsgResponse(js);
                    break;
                case GROUP_CHAT_NOTICE:
                    handleGroupChatNoticeResponse(js);
                    break;
                case FRIEND_MSG:
                    handleFriendMsgResponse(js);
                    break;
                case FRIEND_NOTICE:
                    handleFriendNoticeResponse(js);
                    break;
                case FRIEND_APPLY:
                    handleFriendApplyResponse(js);
                    break;
                case USER_GET_NOTICE:
                    handleUserGetNotice(js);
                    sem_post(&m_rwsem);
                    break;
                case USER_DEAL_NOTICE:
                    handleUserDealNotice(js);
                    sem_post(&m_rwsem);
                    break;
                default:
                    cerr << "Invalid message type received: " << type << endl;
                    break;
            }
        } catch (const json::exception& e) {
            cerr << "Error: Failed to parse JSON data: " << e.what() << endl;
        }
    }
}

// 处理登录回应
void TcpClient::handleLoginResponse(const json& message) {
    int loginstatus = message["loginstatus"].get<int>();
    is_LoginSuccess = false;
    if (NOT_REGISTERED == loginstatus) {
        cout << "account didn't registered!" << endl;
    } else if (WRONG_PASSWD == loginstatus) {
        cout << "wrong password!" << endl;
        // TODO: Handle user information, friend list, group information, etc.
    } else if (IS_ONLINE == loginstatus) {
        cout << "account online" << endl;
        // TODO: Handle user information, friend list, group information, etc.
    } else if (PASS == loginstatus) {
        cout << "Login successful" << endl;
        is_LoginSuccess = true;
        // TODO: Handle user information, friend list, group information, etc.
    } else if (ERR == loginstatus) {
        cout << "make mistakes!" << endl;
    }
}

// 处理注册回应
void TcpClient::handleRegisterResponse(const json& message) {
    int err = message["errno"].get<int>();
    cout << "err:" << err << endl;
    if (REGISTER_SUCCESS == err) {
        cout << "User account register success, user account is "
             << message["account"] << ", do not forget it!" << endl;

    } else if (ACCOUNT_EXIST == err) {
        cerr << "User account is already exist, register error!" << endl;

    } else {
        cout << "made mistakes! register fail!" << endl;
    }
}

// 获取好友列表
void TcpClient::handleFriendListResponse(const json& message) {
    try {
        // cout << "----handleFriendListResponse" << endl;
        m_friendmanager->onlineFriends.clear();
        m_friendmanager->offlineFriends.clear();
        m_friendmanager->onlineFriends = message["online_friends"];
        m_friendmanager->offlineFriends = message["offline_friends"];
    } catch (const exception& e) {
        cout << "handleFriendListResponse error :" << e.what() << endl;
    }
}

// 处理添加好友请求回应
void TcpClient::handleFriendAddResponse(const json& message) {
    int status = message["status"].get<int>();
    if (status == NOT_REGISTERED) {
        cout << "account didn't register" << endl;
    }
    if (status == SUCCESS_ADD_FRIEND) {
        cout << "add friend successfully!" << endl;
    }
    if (status == ALREADY_FRIEND) {
        cout << "已是好友" << endl;
    }
}

// 处理删除好友请求回应
void TcpClient::handleFriendDeleteResponse(const json& message) {
    int status = message["status"].get<int>();
    if (status == NOT_FRIEND) {
        cout << "The person is not your friend" << endl;
    }
    if (status == SUCCESS_DELETE_FRIEND) {
        cout << "delete friend successfully!" << endl;
    }
}

// 处理好友聊天请求回应
void TcpClient::handleFriendChatResponse(const json& message) {
    int status = message["status"].get<int>();
    // cout << "status" << status << endl;
    if (status == NOT_FRIEND) {
        cout << "The person is not your friend" << endl;
    } else if (status == SUCCESS_CHAT_FRIEND) {
        // cout << "chat friend successfully!" << endl;
    } else if (status == GET_FRIEND_HISTORY) {
        std::vector<std::string> msg;
        msg = message["msg"];
        if (msg.size() == 0) {
            cout << "无历史记录" << endl;
            return;
        }

        cout << "----------以下为历史记录----------" << endl;
        for (const auto& entry : msg) {
            json entryjson = json::parse(entry);
            string sender = entryjson["account"];
            string data = entryjson["data"];
            std::time_t timestamp = entryjson["timestamp"];
            std::tm timeinfo;
            localtime_r(&timestamp, &timeinfo);
            std::stringstream ss;
            ss << std::put_time(&timeinfo, "%m-%d %H:%M");
            std::string formattedTime = ss.str();
            if (sender != m_account) {
                std::cout << YELLOW_COLOR << "[好友]" << sender << RESET_COLOR
                          << formattedTime << ":" << std::endl;
                std::cout << "「" << data << "」" << std::endl;
            } else {
                std::cout << BLUE_COLOR << "[我]" << m_username << RESET_COLOR
                          << formattedTime << ":" << std::endl;
                std::cout << "「" << data << "」" << std::endl;
            }
        }
        cout << "----------以上为历史记录----------" << endl;
    } else if (status == ALREADY_TO_FILE) {
        cout << "-----------waiting..." << endl;
    } else if (status == SUCCESS_RECV_FILE) {
        cout << "发送成功" << endl;
    } else if (status == NO_FILE) {
        cout << "当前聊天无文件内容" << endl;
    } else if (status == FILE_LIST) {
        vector<string> filelist = message["filelist"];
        cout << "文件列表如下" << endl;
        for (const auto& entry : filelist) {
            cout << entry << endl;
        }
        cout << "请输入你要接收的文件" << endl;

    } else if (status == ACCESS_FILE_FAIL) {
        cout << "接收失败" << endl;
    } else if (status == GET_FILE_SIZE) {
        string filename = message["filename"];
        filesize = message["filesize"];
        cout << "filesize" << filesize << endl;

        // 创建文件来保存接收的数据
        std::ofstream received_file(filename, std::ios::binary);

        // 接收文件数据并写入文件
        char buffer[4096];
        ssize_t bytes_received;
        cout << "---------start recv----------" << endl;

        size_t sum = 0;
        size_t offset = filesize - sum;
        while (sum < filesize) {
            if (offset < 4096) {
                bytes_received = recv(m_fd, buffer, offset, 0);
            } else {
                bytes_received = recv(m_fd, buffer, sizeof(buffer), 0);
            }
            cout << "--recv :" << bytes_received << "字节" << endl;
            received_file.write(buffer, bytes_received);
            sum += bytes_received;
            offset = filesize - sum;
        }
        received_file.close();
        cout << "recv sum " << sum << "字节" << endl;

    } else if (status == ACCESS_FILE_SUCCESS) {
        cout << "接收成功" << endl;
    }
}

// 处理查询好友请求回应
void TcpClient::handleFriendRequiryResponse(const json& message) {
    int status = message["status"].get<int>();
    if (status == NOT_FRIEND) {
        cout << "The person is not your friend" << endl;
    }
    if (status == SUCCESS_REQUIRY_FRIEND) {
        cout << "requiry friend successfully!" << endl;
    }
}

void TcpClient::handleFriendChatRequiryResponse(const json& message) {
    int status = message["status"].get<int>();
    if (status == NOT_FRIEND) {
        cout << "The person is not your friend" << endl;
    }
    if (status == SUCCESS_REQUIRY_FRIEND) {
        cout << "chatrequiry friend successfully! 输入“:q”退出 "
                "输入“:h”显示历史消息 输入“:f”发送文件 输入“:r”接收文件"
             << endl;
        is_Friend = true;
    }
}
// 处理好友消息回应undefined reference to `GroupManager::Grou
void TcpClient::handleFriendMsgResponse(const json& message) {
    try {
        string sender = message["account"];
        string data = message["data"];
        string username = message["username"];
        std::time_t timestamp = message["timestamp"];
        std::tm timeinfo;
        localtime_r(&timestamp, &timeinfo);
        std::stringstream ss;
        ss << std::put_time(&timeinfo, "%m-%d %H:%M");
        std::string formattedTime = ss.str();
        std::cout << BLUE_COLOR << username << RESET_COLOR << "(" << sender
                  << ")" << formattedTime << ":" << std::endl;
        std::cout << "「" << data << "」" << std::endl;
    } catch (const exception& e) {
        std::cout << "handleFriendMsgResponse error" << e.what() << std::endl;
    }
}

// 处理拉黑好友请求回应
void TcpClient::handleFriendBlockResponse(const json& message) {}

void TcpClient::handleFriendNoticeResponse(const json& message) {
    string account = message["account"];
    string name = message["username"];
    cout << endl;
    cout << "您收到一条消息来自" << name << "(" << account << ")" << std::endl;

}
// 向数据中加入数据长度
void TcpClient::addDataLen(json& js) {
    js["datalen"] = "";
    string prerequest = js.dump();  // 序列化
    int datalen = prerequest.length() + FIXEDWIDTH;
    std::string strNumber = std::to_string(datalen);
    std::string paddedStrNumber =
        std::string(FIXEDWIDTH - strNumber.length(), '0') + strNumber;
    js["datalen"] = paddedStrNumber;
}

// 登录菜单
void TcpClient::welcomeMenu() {
    cout << "========================" << endl;
    cout << "1. login" << endl;
    cout << "2. register" << endl;
    cout << "3. quit" << endl;
    cout << "========================" << endl;
}

// 主菜单
void TcpClient::mainMenu() {
    cout << "               #####################################" << endl;
    cout << "                           1.好友" << endl;
    cout << "                           2.群" << endl;
    cout << "                           3.个人信息 " << endl;
    cout << "                           4.用户通知处理 " << endl;
    cout << "                           5.退出" << endl;
    cout << "               #####################################" << endl;
}

// 得到账号信息
void TcpClient::getInfo(string account) {
    json js;
    js["type"] = GET_INFO;
    addDataLen(js);
    string request = js.dump();
    int len = send(m_fd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (0 == len || -1 == len) {
        cerr << "getInfo send error:" << request << endl;
    }
    sem_wait(&m_rwsem);
}

void TcpClient::handleLogin() {
    std::string account;  // 11位账号id
    std::string pwd;
    cout << "不支持输入空格" << endl;
    cout << "account:";
    cin >> account;
    cout << "userpassword:";
    cin >> pwd;
    json js;
    js["type"] = LOGIN;
    js["account"] = account;
    js["password"] = pwd;
    addDataLen(js);
    string request = js.dump();
    int len = send(m_fd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (len == -1) {
        cerr << "send login msg error:" << request << endl;
    }
    sem_wait(&m_rwsem);  // 等待信号量，由子线程处理完登录的响应消息后，通知这里
    if (is_LoginSuccess) {
        // 初始化个人信息
        getInfo(account);
        m_account = account;
        system("clear");
        // 进入主菜单
        handleMainMenu();
    }
}

void TcpClient::handleMainMenu() {
    while (true) {
        mainMenu();
        int mainMenuChoice;
        bool legal = false;
        while (!legal) {
            cout << "请输入：";
            legal = dataInput(mainMenuChoice);
        }
        switch (mainMenuChoice) {
            case 1:  // 好友
                m_friendmanager->fiendMenu();
                break;
            case 2:
                m_groupmanager->groupMenu();
                break;
            case 3:  // 个人信息
                m_usermanager->showUserInfo();
                break;
            case 4:  // 通知
                m_usermanager->dealNotice();
                break;
            case 5:
                // 返回到上一层菜单
                return;
            default:
                cerr << "invalid input!" << endl;
                break;
        }
    }
}

void TcpClient::handleRegister() {
    std::string account;
    std::string name;
    std::string pwd;
    cout << "不支持输入空格" << endl;
    cout << "account(11位以内):";
    cin >> account;
    if (account.length() > 11) {
        std::cout << "Input exceeded the maximum allowed length. Truncating..."
                  << std::endl;
        account = account.substr(0, 11);  // Truncate the input to 11 characters
    }
    cout << "username(20字符以内):";
    cin >> name;
    if (name.length() > 20) {
        std::cout << "Input exceeded the maximum allowed length. Truncating..."
                  << std::endl;
        account = account.substr(0, 20);  // Truncate the input to 20 characters
    }
    cout << "userpassword(20字符以内):";
    cin >> pwd;
    cin.get();
    if (pwd.length() > 20) {
        std::cout << "Input exceeded the maximum allowed length. Truncating..."
                  << std::endl;
        account = account.substr(0, 20);  // Truncate the input to 20 characters
    }
    json js;
    js["type"] = REG;
    js["username"] = name;
    js["account"] = account;
    js["password"] = pwd;
    addDataLen(js);
    string request = js.dump();

    int len = send(m_fd, request.c_str(), strlen(request.c_str()) + 1, 0);
    if (len == -1) {
        cerr << "send reg msg error:" << request << endl;
    }

    sem_wait(&m_rwsem);  // 等待信号量，子线程处理完注册消息会通知

    cout << "pthread work successfully" << endl;
}

void TcpClient::handleGroupListResponse(const json& message) {
    try {
        m_groupmanager->userGroups.clear();
        cout << "----handleGroupListResponse" << endl;
        m_groupmanager->userGroups = message["usergroups"];

    } catch (const exception& e) {
        cout << "handleGroupListResponse error :" << e.what() << endl;
    }
}

void TcpClient::handleGroupAddResponse(const json& message) {
    int status = message["status"].get<int>();
    if (status == NOT_REGISTERED) {
        cout << "group did't register" << endl;
    } else if (status == SUCCESS_SEND_APPLICATION) {
        cout << "success send application" << endl;
    } else {
        cout << "fail to send application" << endl;
    }
}

void TcpClient::handleGroupCreateResponse(const json& message) {
    int status = message["status"].get<int>();
    if (status == SUCCESS_CREATE_GROUP) {
        string id = message["groupid"];
        cout << "create group successfully" << endl;
        cout << "don't forget ,your groupid is " << id << endl;
    } else {
        cout << "fail to create group" << endl;
    }
}
void TcpClient::handleGroupEnterResponse(const json& message) {
    m_permission = message["permission"];
}

void TcpClient::handleGroupRequiryResponse(const json& message) {}

void TcpClient::handleGroupOwnerResponse(const json& message) {
    int entertype = message["entertype"];
    if (entertype == OWNER_CHAT) {
        chatResponse(message);
    } else if (entertype == OWNER_KICK) {
        ownerKick(message);
    } else if (entertype == OWNER_ADD_ADMINISTRATOR) {
        ownerAddAdministrator(message);
    } else if (entertype == OWNER_REVOKE_ADMINISTRATOR) {
        ownerRevokeAdministrator(message);
    } else if (entertype == OWNER_CHECK_MEMBER) {
        ownerCheckMember(message);
    } else if (entertype == OWNER_CHECK_HISTORY) {
        ownerCheckHistory(message);
    } else if (entertype == OWNER_NOTICE) {
        ownerNotice(message);
    } else if (entertype == OWNER_CHANGE_NAME) {
        ownerChangeName(message);
    } else if (entertype == OWNER_DISSOLVE) {
        ownerDissolve(message);
    }
}
void TcpClient::handleGroupAdminResponse(const json& message) {
    int entertype = message["entertype"];
    if (entertype == ADMIN_CHAT) {
        chatResponse(message);
    } else if (entertype == ADMIN_KICK) {
        adminKick(message);
    } else if (entertype == ADMIN_CHECK_MEMBER) {
        adminCheckMember(message);
    } else if (entertype == ADMIN_CHECK_HISTORY) {
        adminCheckHistory(message);
    } else if (entertype == ADMIN_NOTICE) {
        adminNotice(message);
    } else if (entertype == ADMIN_EXIT) {
        adminExit(message);
    }
}
void TcpClient::handleGroupMemberResponse(const json& message) {
    int entertype = message["entertype"];
    if (entertype == MEMBER_CHAT) {
        chatResponse(message);
    } else if (entertype == MEMBER_CHECK_MEMBER) {
        memberCheckMember(message);
    } else if (entertype == MEMBER_CHECK_HISTORY) {
        memberCheckHistory(message);
    } else if (entertype == MEMBER_EXIT) {
        memberExit(message);
    }
}

void TcpClient::ownerKick(const json& message) {
    int status = message["status"];
    if (status == SUCCESS_KICK) {
        cout << "已踢出该成员" << endl;
    } else if (status == FAIL_TO_KICK) {
        cout << "不能踢出该成员" << endl;
    }
}
// 回应 添加管理员
void TcpClient::ownerAddAdministrator(const json& message) {
    int status = message["status"];
    if (status == NOT_SELF) {
        cout << "您已是群主，不可以把自己设为管理员" << endl;
    } else if (status == ADMIN_ALREADY_EXIST) {
        cout << "该成员已是管理员" << endl;
    } else if (status == NOT_MEMBER) {
        cout << "不是群成员" << endl;
    } else if (status == ADMIN_ADD_SUCCESS) {
        cout << "添加成功" << endl;
    } else {
        cout << "fail to add administration" << endl;
    }
}

void TcpClient::ownerRevokeAdministrator(const json& message) {
    int status = message["status"];
    if (status == SUCCESS_REVOKE_ADMIN) {
        cout << "成功撤除该管理员" << endl;
    } else {
        cout << "撤除该管理员失败" << endl;
    }
}
void TcpClient::ownerCheckMember(const json& message) {
    string owner = message["owner"];
    unordered_set<string> admin;
    unordered_set<string> member;
    admin = message["administrator"];
    member = message["member"];
    cout << YELLOW_COLOR << "群主:" << owner << RESET_COLOR << endl;
    for (const auto& entry : admin) {
        cout << GREEN_COLOR << "管理员:" << entry << RESET_COLOR << endl;
    }
    for (const auto& entry : member) {
        cout << "成员:" << entry << endl;
    }
}
void TcpClient::ownerCheckHistory(const json& message) {}
void TcpClient::ownerNotice(const json& message) {
    int status = message["status"];
    if (status == SUCCESS_ACCEPT_MEMBER) {
        cout << "您已成功接受该申请" << endl;
    } else if (status == SUCCESS_REFUSE_MEMBER) {
        cout << "您已成功拒接该申请" << endl;
    } else {
        cout << "处理该申请失败" << endl;
    }
}
void TcpClient::ownerChangeName(const json& message) {}
void TcpClient::ownerDissolve(const json& message) {
    int status = message["status"].get<int>();
    if (status == DISSOLVE_FAIL) {
        cout << "fail to dissolve" << endl;
    } else {
        cout << "dissolve successfully" << endl;
    }
}

void TcpClient::adminKick(const json& message) {
    int status = message["status"];
    if (status == SUCCESS_KICK) {
        cout << "已踢出该成员" << endl;
    } else if (status == FAIL_TO_KICK) {
        cout << "不能踢出该成员" << endl;
    }
}
void TcpClient::adminCheckMember(const json& message) {
    string owner = message["owner"];
    unordered_set<string> admin;
    unordered_set<string> member;
    admin = message["administrator"];
    member = message["member"];
    cout << YELLOW_COLOR << "群主:" << owner << RESET_COLOR << endl;
    for (const auto& entry : admin) {
        cout << GREEN_COLOR << "管理员:" << entry << RESET_COLOR << endl;
    }
    for (const auto& entry : member) {
        cout << "成员:" << entry << endl;
    }
}
void TcpClient::adminCheckHistory(const json& message) {}
void TcpClient::adminNotice(const json& message) {
    int status = message["status"];
    if (status == SUCCESS_ACCEPT_MEMBER) {
        cout << "您已成功接受该申请" << endl;
    } else if (status == SUCCESS_REFUSE_MEMBER) {
        cout << "您已成功拒接该申请" << endl;
    } else {
        cout << "处理该申请失败" << endl;
    }
}
// 管理员退出
void TcpClient::adminExit(const json& message) {
    int status = message["status"];
    if (status == FAIL_TO_EXIT) {
        cout << "退出失败" << endl;
    } else if (status == SUCCESS_EXIT) {
        cout << "退出成功" << endl;
    }
}

void TcpClient::memberCheckMember(const json& message) {
    string owner = message["owner"];
    unordered_set<string> admin;
    unordered_set<string> member;
    admin = message["administrator"];
    member = message["member"];
    cout << YELLOW_COLOR << "群主:" << owner << RESET_COLOR << endl;
    for (const auto& entry : admin) {
        cout << GREEN_COLOR << "管理员:" << entry << RESET_COLOR << endl;
    }
    for (const auto& entry : member) {
        cout << "成员:" << entry << endl;
    }
}
void TcpClient::memberCheckHistory(const json& message) {}
// 成员退出
void TcpClient::memberExit(const json& message) {
    int status = message["status"];
    if (status == FAIL_TO_EXIT) {
        cout << "退出失败" << endl;
    } else if (status == SUCCESS_EXIT) {
        cout << "退出成功" << endl;
    }
}

void TcpClient::handleGroupGetNoticeResponse(const json& message) {
    m_groupnotice.clear();
    m_groupnotice =
        message["notice"];  // msg字符串是json格式存储的，需要序列化再使用
    for (size_t i = 0; i < m_groupnotice.size(); ++i) {
        json info = json::parse(m_groupnotice[i]);
        string type = info["type"];
        if (type == "add") {
            string source = info["source"];
            string msg = info["msg"];
            cout << i << "、"
                 << "用户：" << source << "发送了一条入群申请" << endl;
            cout << "对方留言：" << msg << endl;
            if (info.contains("dealer")) {
                string dealer = info["dealer"];
                string result = info["result"];
                cout << "处理人：" << dealer << " 处理结果：" << result << endl;
            }

        } else if (type == "kick") {
            ;
        } else if (type == "promote") {
            string member = info["member"];
            string source = info["source"];
            cout << i << "、"
                 << "成员：" << member << "被群主：" << source
                 << "设置为了管理员" << endl;

        } else if (type == "quit") {
            ;
        } else if (type == "revoke") {
            string dealer = info["dealer"];
            string member = info["member"];
            cout << i << "、"
                 << "成员：" << member << "被群主：" << dealer
                 << "撤除了管理员身份" << endl;
        }
        cout << "---------------------------------------------------" << endl;
    }
}

void TcpClient::handleGroupMsgResponse(const json& message) {
    try {
        string sender = message["account"];
        string data = message["data"];
        string username = message["username"];
        string permission = message["permission"];
        std::time_t timestamp = message["timestamp"];
        std::tm timeinfo;
        localtime_r(&timestamp, &timeinfo);
        std::stringstream ss;
        ss << std::put_time(&timeinfo, "%m-%d %H:%M");
        std::string formattedTime = ss.str();
        if (permission == "owner") {
            std::cout << YELLOW_COLOR << "[群主]" << username << RESET_COLOR
                      << "(" << sender << ")" << formattedTime << ":"
                      << std::endl;
            std::cout << "「" << data << "」" << std::endl;
        } else if (permission == "administrator") {
            std::cout << GREEN_COLOR << "[管理员]" << username << RESET_COLOR
                      << "(" << sender << ")" << formattedTime << ":"
                      << std::endl;
            std::cout << "「" << data << "」" << std::endl;
        } else {
            std::cout << "[成员]" << username << "(" << sender << ")"
                      << formattedTime << ":" << std::endl;
            std::cout << "「" << data << "」" << std::endl;
        }

    } catch (const exception& e) {
        std::cout << "handleFriendMsgResponse error" << e.what() << std::endl;
    }
}

void TcpClient::handleGroupChatNoticeResponse(const json& message) {
    string groupid = message["id"];
    string groupname = message["groupname"];
    cout << endl;
    cout << "您收到一条消息来自群聊" << groupname << "(" << groupid << ")"
         << std::endl;

}
void TcpClient::handleSetChatAckResponse(const json& message) {
    int status = message["status"].get<int>();
    if (status == SUCCESS_SET_CHATSTATUS) {
        cout << "set chatstatus successfully! 输入“:q”退出 输入“ : "
                "h”显示历史消息 输入“ : f”发送文件 输入“ : r”接收文件 "
             << endl;
    }
}
void TcpClient::handleGetListLenResponse(const json& message) {
    len = message["len"].get<int>();
}

void TcpClient::chatResponse(const json& message) {
    int status = message["status"].get<int>();
    if (status == FAIL_SEND_MSG) {
        cout << "fail to send msg" << endl;
    } else if (status == GET_FRIEND_HISTORY) {
        std::vector<std::string> msg;
        msg = message["msg"];
        if (msg.size() == 0) {
            cout << "无历史记录" << endl;
            return;
        }
        cout << "----------以下为历史记录----------" << endl;
        for (const auto& entry : msg) {
            json entryjson = json::parse(entry);
            string sender = entryjson["account"];
            string permission = entryjson["permission"];
            string data = entryjson["data"];
            std::time_t timestamp = entryjson["timestamp"];
            std::tm timeinfo;
            localtime_r(&timestamp, &timeinfo);
            std::stringstream ss;
            ss << std::put_time(&timeinfo, "%m-%d %H:%M");
            std::string formattedTime = ss.str();
            if (sender != m_account) {
                if (permission == "owner") {
                    std::cout << YELLOW_COLOR << "[群主]" << sender
                              << RESET_COLOR << formattedTime << ":"
                              << std::endl;
                    std::cout << "「" << data << "」" << std::endl;
                } else if (permission == "administrator") {
                    std::cout << GREEN_COLOR << "[管理员]" << sender
                              << RESET_COLOR << formattedTime << ":"
                              << std::endl;
                    std::cout << "「" << data << "」" << std::endl;
                } else if (permission == "member") {
                    std::cout << "[成员]" << sender << formattedTime << ":"
                              << std::endl;
                    std::cout << "「" << data << "」" << std::endl;
                }
            } else {
                if (permission == "owner") {
                    std::cout << YELLOW_COLOR << "[群主]"
                              << "我" << RESET_COLOR << formattedTime << ":"
                              << std::endl;
                    std::cout << "「" << data << "」" << std::endl;
                } else if (permission == "administrator") {
                    std::cout << GREEN_COLOR << "[管理员]"
                              << "我" << RESET_COLOR << formattedTime << ":"
                              << std::endl;
                    std::cout << "「" << data << "」" << std::endl;
                } else if (permission == "member") {
                    std::cout << BLUE_COLOR << "[成员]"
                              << "我" << RESET_COLOR << formattedTime << ":"
                              << std::endl;
                    std::cout << "「" << data << "」" << std::endl;
                }
            }
        }
        cout << "----------以上为历史记录----------" << endl;
    } else if (status == ALREADY_TO_FILE) {
        cout << "-----------waiting..." << endl;
    } else if (status == SUCCESS_RECV_FILE) {
        cout << "发送成功" << endl;
    } else if (status == NO_FILE) {
        cout << "当前聊天无文件内容" << endl;
    } else if (status == FILE_LIST) {
        vector<string> filelist = message["filelist"];
        cout << "文件列表如下" << endl;
        for (const auto& entry : filelist) {
            cout << entry << endl;
        }
        cout << "请输入你要接收的文件" << endl;

    } else if (status == ACCESS_FILE_FAIL) {
        cout << "接收失败" << endl;
    } else if (status == GET_FILE_SIZE) {
        string filename = message["filename"];
        size_t filesize = message["filesize"];
        cout << "filesize" << filesize << endl;
        sem_post(&m_rwsem);
        // 创建文件来保存接收的数据
        std::ofstream received_file(filename, std::ios::binary);

        // 接收文件数据并写入文件
        char buffer[4096];
        ssize_t bytes_received;
        cout << "---------start recv----------" << endl;
        size_t sum = 0;
        size_t offset = filesize - sum;
        while (sum < filesize) {
            if (offset < 4096) {
                bytes_received = recv(m_fd, buffer, offset, 0);
            } else {
                bytes_received = recv(m_fd, buffer, sizeof(buffer), 0);
            }
            cout << "--recv :" << bytes_received << "字节" << endl;
            received_file.write(buffer, bytes_received);
            sum += bytes_received;
            offset = filesize - sum;
        }
        received_file.close();
        cout << "recv sum " << sum << "字节" << endl;

    } else if (status == ACCESS_FILE_SUCCESS) {
        cout << "接收成功" << endl;
    }
}
void TcpClient::handleFriendApplyResponse(const json& message) {
    string account = message["account"];
    string username = message["username"];
    cout << endl;
    cout << "您收到一条好友申请来自" << username << "(" << account << ")"
         << std::endl;

}

void TcpClient::handleUserGetNotice(const json& message) {
    m_usernotice.clear();
    m_usernotice = message["notice"];
    for (size_t i = 0; i < m_usernotice.size(); ++i) {
        json info = json::parse(m_usernotice[i]);
        string type = info["type"];
        if (type == "friendadd") {
            string source = info["src"];
            string msg = info["msg"];
            cout << i << "、"
                 << "用户：" << source << "给你发送了一条好友申请" << endl;
            cout << "对方留言：" << msg << endl;
            if (info.contains("dealer")) {
                string dealer = info["dealer"];
                string result = info["result"];
                cout << "处理人：" << dealer << " 处理结果：" << result << endl;
            }

        } else if (type == "kick") {
            ;
        }
        cout << "---------------------------------------------------" << endl;
    }
}

void TcpClient::handleUserDealNotice(const json& message) {
    m_status = message["status"];
}
