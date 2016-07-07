#include "zp_worker_thread.h"

#include <glog/logging.h>

ZPWorkerThread::ZPWorkerThread(int cron_interval)
  : WorkerThread::WorkerThread(cron_interval),
    thread_querynum_(0),
    last_thread_querynum_(0),
    last_time_us_(slash::NowMicros()),
    last_sec_thread_querynum_(0) {
      cmds_.reserve(300);
      InitClientCmdTable(&cmds_);
    }

ZPWorkerThread::~ZPWorkerThread() {
  DestoryCmdTable(cmds_);

  should_exit_ = true;
  pthread_join(thread_id(), NULL);
  LOG(INFO) << "A worker thread " << thread_id() << " exit!!!";
}

void ZPWorkerThread::CronHandle() {

  ResetLastSecQuerynum();

  {
    struct timeval now;
    gettimeofday(&now, NULL);
    slash::RWLock l(&rwlock_, false); // Use ReadLock to iterate the conns_
    std::map<int, void*>::iterator iter = conns_.begin();

    while (iter != conns_.end()) {

      // TODO simple 3s
      if (now.tv_sec - static_cast<ZPClientConn*>(iter->second)->last_interaction().tv_sec > 3) {
        LOG(INFO) << "Find Timeout Client: " << static_cast<ZPClientConn*>(iter->second)->ip_port();
        AddCronTask(WorkerCronTask{TASK_KILL, static_cast<ZPClientConn*>(iter->second)->ip_port()});
      }
      iter++;
    }
  }

  {
    slash::MutexLock l(&mutex_);
    while (!cron_tasks_.empty()) {
      WorkerCronTask t = cron_tasks_.front();
      cron_tasks_.pop();
      mutex_.Unlock();
      LOG(INFO) << "ZPWorkerThread, Got a WorkerCronTask";
      switch (t.task) {
        case TASK_KILL:
          ClientKill(t.ip_port);
          break;
        case TASK_KILLALL:
          ClientKillAll();
          break;
      }
      mutex_.Lock();
    }
  }
}

bool ZPWorkerThread::ThreadClientKill(std::string ip_port) {

  if (ip_port == "") {
    AddCronTask(WorkerCronTask{TASK_KILLALL, ""});
  } else {
    if (!FindClient(ip_port)) {
      return false;
    }
    AddCronTask(WorkerCronTask{TASK_KILL, ip_port});
  }
  return true;
}

int ZPWorkerThread::ThreadClientNum() {
  slash::RWLock l(&rwlock_, false);
  return conns_.size();
}

void ZPWorkerThread::AddCronTask(WorkerCronTask task) {
  slash::MutexLock l(&mutex_);
  cron_tasks_.push(task);
}

bool ZPWorkerThread::FindClient(std::string ip_port) {
  slash::RWLock l(&rwlock_, false);
  std::map<int, void*>::iterator iter;
  for (iter = conns_.begin(); iter != conns_.end(); iter++) {
    if (static_cast<ZPClientConn*>(iter->second)->ip_port() == ip_port) {
      return true;
    }
  }
  return false;
}

void ZPWorkerThread::ClientKill(std::string ip_port) {
  slash::RWLock l(&rwlock_, true);
  std::map<int, void*>::iterator iter;
  for (iter = conns_.begin(); iter != conns_.end(); iter++) {
    if (static_cast<ZPClientConn*>(iter->second)->ip_port() != ip_port) {
      continue;
    }
    LOG(INFO) << "==========Kill Client==============";
    close(iter->first);
    delete(static_cast<ZPClientConn*>(iter->second));
    conns_.erase(iter);
    break;
  }
}

void ZPWorkerThread::ClientKillAll() {
  slash::RWLock l(&rwlock_, true);
  std::map<int, void*>::iterator iter = conns_.begin();
  while (iter != conns_.end()) {
    LOG(INFO) << "==========Kill Client==============";
    close(iter->first);
    delete(static_cast<ZPClientConn*>(iter->second));
    iter = conns_.erase(iter);
  }
}

//int64_t ZPWorkerThread::ThreadClientList(std::vector<ClientInfo> *clients) {
//  slash::RWLock l(&rwlock_, false);
//  if (clients != NULL) {
//    std::map<int, void*>::const_iterator iter = conns_.begin();
//    while (iter != conns_.end()) {
//      clients->push_back(ClientInfo{iter->first, reinterpret_cast<ZPClientConn*>(iter->second)->ip_port(), static_cast<int>((reinterpret_cast<ZPClientConn*>(iter->second)->last_interaction()).tv_sec)});
//      iter++;
//    }
//  }
//  return conns_.size();
//}