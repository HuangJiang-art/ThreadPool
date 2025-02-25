#include"threadpool.h"

const int TASK_MAX_THRESHOLD =	INT32_MAX;
const int THREAD_MAX_THRESHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60;//单位:s

//线程池构造
ThreadPool::ThreadPool()
	:initThreadSize_(0)
	,taskSize_(0)
	,taskQueMaxThresHold_(TASK_MAX_THRESHOLD)
	, threadSizeThresHold_(THREAD_MAX_THRESHOLD)
	, threadPool_(Poo lMode::MODE_FIXED)
	, isStart_(false)
	, idleThreadSize(0)
	, curThreadSize_(0)
{}

//线程池析构
ThreadPool::~ThreadPool()
{
	isStart_ = false;

	//wait all thread return  阻塞&正在执行任务
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });//每个线程结束时要notify 否则主线程这里会阻塞
}
void ThreadPool::setMode(PoolMode mode) //设置线程池工作模式
{
	if (checkRuningState())//如果已经声明过直接返回
	{
		return;
	}
	threadPool_ = mode; 
}

void ThreadPool::setTaskQueMaxThresHold(int threshold)//设置Task任务队列上线阈值
{
	if (checkRuningState())//如果已经声明过直接返回
	{
		return;
	}
	taskQueMaxThresHold_ = threshold;
}

void ThreadPool::setThreadSizeThresHold(int threshold)//设置cached模式下线程的上限阈值
{
	if (checkRuningState())
	{
		return;
	}
	if (threadPool_ == PoolMode::MODE_CACHED)
	{
		threadSizeThresHold_ = threshold;
	}
}

Result ThreadPool::submitTask(std::shared_ptr<Task> sp)//给线程池提交任务
{
	//获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);
    
	//线程通信  等待任务队列有空余
	//while (taskQue_.size() == taskQueMaxThresHold_)
	//{
	//	notFull_.wait(lock);
	//}
	if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThresHold_; }))
	{
		//表示notFull_等待1s钟，条件依然没有满足
		std::cerr << "task queue is full,submit task fail." << std::endl;
		return Result(sp, false);
	}
	//别人通知 条件满足
	//用户提交任务，最长不能超过1s，否则判断提交失败，返回  

	//如果有空余，把任务放入任务列表
	taskQue_.emplace(sp);
	taskSize_++;

	// 在notEmpty_上进行通知 赶快分配线程执行任务
	notEmpty_.notify_all();

	//cached模式:根据任务的数量和空闲线程的数量判断是否需要创建新的线程出来->小而快的任务
	if (threadPool_ == PoolMode::MODE_CACHED&&taskSize_>idleThreadSize&&curThreadSize_<threadSizeThresHold_)
	{
		std::cout << "create new thread!" << std::endl;
		//创建新线程
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));//括号里面是创建对象时需要的参数
		int threadid = ptr->getId();
		threads_.emplace(threadid,std::move(ptr));
		threads_[threadid]->start();//启动线程
		idleThreadSize++;
		curThreadSize_++;
	}
	return Result(sp);
}

void ThreadPool::start(int initThreadSize)//开启线程池
{
	//设置运行状态
	isStart_ = true;

	//记录初始线程个数
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;
	//创建线程对象

	for (int i = 0; i < initThreadSize_; i++)
	{
		//创建thread对象的时候，把线程函数给到thread对象
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));//括号里面是创建对象时需要的参数
		//threads_.emplace_back(std::move(ptr));
		int threadid = ptr->getId();
		threads_.emplace(threadid, std::move(ptr));
	}

	//启动所有线程
	//std::vector<Thread*> threads_;//线程列表
	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start();//需要去执行一个线程函数
		idleThreadSize++;//记录初始空闲线程的数量
	}
}

//定义线程函数 从任务队列里面消费任务
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();//定义一个for循环外的变量
	//线程池里所有任务都要处理完才能结束
	for(;;)
	{
		std::shared_ptr<Task> task;
		{
			//先获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "尝试获取任务..." << std::this_thread::get_id()<<std::endl;

			//cahched模式下 可能创建了很多线程，但空闲时间超过60s，应该把多余的线程结束回收(超过initThreadSize_数量的线程要回收)
			while (taskQue_.size() == 0)//当任务队列里面没有任务时触发
			{
				if (!isStart_)//线程池结束 回收资源
				{
					//在执行任务后结束
					threads_.erase(threadid);//删除线程对象
					//curThreadSize_--;
					//idleThreadSize--;
					std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
					exitCond_.notify_all();
					return;//线程函数结束
				}
				if (threadPool_ == PoolMode::MODE_CACHED)
				{
					//条件变量，超时返回了
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
						{
							//开始回收当前线程
							//记录线程数量的相关变量的值修改
							//把线程对象从线程列表容器中删除
							threads_.erase(threadid);//删除线程对象
							curThreadSize_--;
							idleThreadSize--;
							std::cout << "threadid:" << std::this_thread::get_id() <<"exit"<< std::endl;
							return;
						}
					}
				}
				else
				{
					//等待notEmpty
					notEmpty_.wait(lock);
				}
				//if (!isStart_)//线程池要回收资源
				//{
				//	//在阻塞后结束线程
				//	threads_.erase(threadid);//删除线程对象
				//	//curThreadSize_--;
				//	//idleThreadSize--;
				//	std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
				//	exitCond_.notify_all();
				//	return;
				//}
			}
			//有任务可以取
			idleThreadSize--;//线程数量少一

			std::cout << "获取任务成功..." << std::this_thread::get_id()<< std::endl;
			//从任务队列中取一个任务出来
			 task = taskQue_.front();
			taskQue_.pop();
			taskSize_--; 

			//如果依然有剩余任务，继续通知其他的线程执行任务
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			//取出一个任务 通知等待的生产任务
			notFull_.notify_all();
		}

		//当前线程负责执行这个任务
		if (task != nullptr)
		{
			task->exec();
			//执行完任务进行通知

		}
		idleThreadSize++;//处理完毕
		lastTime = std::chrono::high_resolution_clock().now(); //更新线程执行完任务的时间
	}
}

bool ThreadPool::checkRuningState()const
{
	return isStart_;
}
//线程方法实现
int Thread::generateId_ = 0;
Thread::Thread(ThreadFunc func)
	:func_(func)
	,threadId_(generateId_++)
{}

Thread::~Thread()
{

}
void Thread::start()
{
	//创建一个线程来执行一个线程函数
	std::thread t(func_,threadId_);//C++11 线程对象t 和线程函数func_ 
	t.detach(); //设置分离线程
}
int  Thread::getId()const
{
	return threadId_;
}

//任务类
Task::Task()
	:result_(nullptr)
{}
void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run());//这里发生多态调用
	}
}
void Task::setResult(Result* res)
{
	result_ = res;
}


//////返回值对象接收类
Result::Result(std::shared_ptr<Task> task, bool isValid)
	:task_(task)
	,isValid_(isValid)
{
	task->setResult(this);
}

Any Result::get()
{
	
	if (!isValid_)
	{
		return "";
	}
	sem_.wait();//task没执行完 会阻塞
	return std::move(any_);//只接受右值进行拷贝构造

}
void Result::setVal(Any any)
{
	//存储task的返回值
	this->any_ = std::move(any);
	sem_.post();
}