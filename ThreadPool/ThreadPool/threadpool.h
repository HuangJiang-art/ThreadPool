#ifndef THREADPOOL_H
#define THREADPOOL_H
#include<stdio.h>
#include<iostream>
#include<vector>
#include<queue>
#include<memory>
#include<atomic>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<thread>
#include<unordered_map>


//Any类型:可以接收任意数据类型
class Any
{
public:
	Any() = default;
	~Any() = default;

	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;

	Any(Any&&)=default;
	Any& operator=(Any&&) = default;

	//这个构造函数让Any类型接收任意其他数据
	template<typename T>
	Any(T data)
		:base_(std::make_unique<Derive<T>>(data))
	{}


	//这个方法能把Any对象里面存储的数据返回
	template<typename T>
	T cast_()
	{
		//基类指针-> 派生类对象 RTTI
		Derive<T>* ptr = dynamic_cast<Derive<T>*>(base_.get());
		if (ptr == nullptr)
		{
			throw "type is unmatch";
		}
		return ptr ->data_;
	}


private:
	class Base//基类
	{
	public:
		virtual ~Base() = default;
	};


	template<typename T>
	class Derive :public Base//派生类
	{
	public:
		Derive(T data)
			:data_(data)
		{}
		T data_;//保存了任意其他类型
	};

private:
	//定义一个基类指针
	std::unique_ptr<Base> base_;
};


//实现一个信号量类
class Semaphore
{
public:
	Semaphore(int limit = 0)
		:resLimit_(limit)
	{}
	~Semaphore() = default;

	void wait()//获取一个信号量资源
	{
		std::unique_lock<std::mutex> lock(mtx_);
		//等待信号量有资源，没有资源的话，会阻塞当前线程
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--; 
	}
	void post()//增加一个信号量资源
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}
private:
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};


//接收任务执行完成后的返回值类型Result
class Task;

class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	//linux有些情况下 对象已经析构了 而对象里面的资源没有析构 mutex semaphore
	~Result() = default;

	//setVal方法，获取执行完的返回值
	void setVal(Any any);


	// get方法，用户调用这个方法获取task的返回值
	Any get();
private:
	Any any_;//存储任务返回值
	Semaphore sem_;//线程通信信号量
	std::shared_ptr<Task> task_;
	std::atomic_bool isValid_;
};


class Task
{
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);
	virtual Any run() = 0;
private:
	Result* result_;
};

enum class PoolMode//使用时要加上作用域
{
	MODE_FIXED,
	MODE_CACHED,
};

//线程类型
class Thread
{
public:
	//线程函数对象类型
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func);

	~Thread();

	//启动线程
	void start();
	int getId()const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;//保存线程ID
};




//线程池类型
class ThreadPool
{
public:
	ThreadPool();//构造
	~ThreadPool();//析构
	void start(int initThreadSize=std::thread::hardware_concurrency());//开启线程池


	//设置task任务队列上限阈值
	void setTaskQueMaxThresHold(int threshold);

	//设置cached模式下线程的上限阈值
	void setThreadSizeThresHold(int threshold);

	Result submitTask(std::shared_ptr<Task> sp);//new 出来
	 
	void setMode(PoolMode mode);


	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:


	//定义线程函数 
	void threadFunc(int threadid);//具体实现的函数还是由线程池提供的

	bool checkRuningState()const; 

private:
	//std::vector<std::unique_ptr<Thread>> threads_;//线程列表
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;//线程列表
	int initThreadSize_;//初始化线程的数量
	std::atomic_int curThreadSize_;//记录当前线程池线程数量
	std::atomic_int idleThreadSize;	//记录空闲线程的数量
	int threadSizeThresHold_;//线程数量的上限阈值

	std::queue<std::shared_ptr<Task>> taskQue_;//任务队列 用强智能指针可以增加对象的生命周期 New一个
	std::atomic_int taskSize_;//任务的数量
	int taskQueMaxThresHold_;//任务队列数量上线阈值

	std::mutex taskQueMtx_;//保证任务队列的线程安全
	std::condition_variable notFull_;//表示任务队列不满
	std::condition_variable notEmpty_;//表示任务队列不空
	std::condition_variable exitCond_;//等待线程资源全部回收

	PoolMode threadPool_;

	//表示当前线程池的启动状态
	std::atomic_bool isStart_;


};



#endif // !THREADPOOL_H

