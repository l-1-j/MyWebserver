#include"sql_connection_pool.h"
connection_pool::connection_pool():freeConn_(0),maxConn_(0),port_(3306){}

connection_pool::~connection_pool(){
    destroyPool();
}

connection_pool*connection_pool::getInstance(){
    static connection_pool pool;
    return &pool;
}

void connection_pool::init(const std::string& url, const std::string& user,
                           const std::string& passwd, const std::string& dbname,
                           int port, int maxConn) {
    url_ = url; user_ = user; passwd_ = passwd;
    dbname_ = dbname; port_ = port; maxConn_ = maxConn;

    for (int i = 0; i < maxConn; i++) {
        MYSQL* con = mysql_init(nullptr);
        if (con == nullptr) {
            continue;
        }
        // 不覆盖 con，直接用 con 连，失败再 close
        if (mysql_real_connect(con, url_.c_str(), user_.c_str(),
                               passwd_.c_str(), dbname_.c_str(),
                               port_, nullptr, 0) == nullptr) {
            mysql_close(con);   // 失败：释放 mysql_init 的内存
            continue;
        }
        connList_.push_back(con);
        freeConn_++;
    }
    sem_init(&sem_, 0, freeConn_);
}

MYSQL*connection_pool::getConnection(){
    sem_wait(&sem_);
    std::lock_guard<std::mutex>lock(mtx_);
    MYSQL*con=connList_.front();
    connList_.pop_front();
    freeConn_--;
    return con;
}

bool connection_pool::releaseConnection(MYSQL*conn){
    if(conn==nullptr)return false;
    {
      std::lock_guard<std::mutex>lock(mtx_);
      connList_.push_back(conn);
      freeConn_++;
    }
    sem_post(&sem_);
    return true;
}

int connection_pool::getFreeConn(){
    return freeConn_;
}

void connection_pool::destroyPool(){
    std::lock_guard<std::mutex>lock(mtx_);
    for(MYSQL*con:connList_){
        mysql_close(con);
    }
    connList_.clear();
    freeConn_=0;
}

connectionRAII::connectionRAII(MYSQL**conn,connection_pool*pool)
    :conn_(nullptr),pool_(pool){
    *conn=pool->getConnection();  
    conn_=*conn;
}

connectionRAII::~connectionRAII(){
    pool_->releaseConnection(conn_);
}
    




    




