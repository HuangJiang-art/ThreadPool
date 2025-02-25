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


//Any����:���Խ���������������
class Any
{
public:
	Any() = default;
	~Any() = default;

	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;

	Any(Any&&)=default;
	Any& operator=(Any&&) = default;

	//������캯����Any���ͽ���������������
	template<typename T>
	Any(T data)
		:base_(std::make_unique<Derive<T>>(data))
	{}


	//��������ܰ�Any��������洢�����ݷ���
	template<typename T>
	T cast_()
	{
		//����ָ��-> ��������� RTTI
		Derive<T>* ptr = dynamic_cast<Derive<T>*>(base_.get());
		if (ptr == nullptr)
		{
			throw "type is unmatch";
		}
		return ptr ->data_;
	}


private:
	class Base//����
	{
	public:
		virtual ~Base() = default;
	};


	template<typename T>
	class Derive :public Base//������
	{
	public:
		Derive(T data)
			:data_(data)
		{}
		T data_;//������������������
	};

private:
	//����һ������ָ��
	std::unique_ptr<Base> base_;
};


//ʵ��һ���ź�����
class Semaphore
{
public:
	Semaphore(int limit = 0)
		:resLimit_(limit)
	{}
	~Semaphore() = default;

	void wait()//��ȡһ���ź�����Դ
	{
		std::unique_lock<std::mutex> lock(mtx_);
		//�ȴ��ź�������Դ��û����Դ�Ļ�����������ǰ�߳�
		cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
		resLimit_--; 
	}
	void post()//����һ���ź�����Դ
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


//��������ִ����ɺ�ķ���ֵ����Result
class Task;

class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	//linux��Щ����� �����Ѿ������� �������������Դû������ mutex semaphore
	~Result() = default;

	//setVal��������ȡִ����ķ���ֵ
	void setVal(Any any);


	// get�������û��������������ȡtask�ķ���ֵ
	Any get();
private:
	Any any_;//�洢���񷵻�ֵ
	Semaphore sem_;//�߳�ͨ���ź���
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

enum class PoolMode//ʹ��ʱҪ����������
{
	MODE_FIXED,
	MODE_CACHED,
};

//�߳�����
class Thread
{
public:
	//�̺߳�����������
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func);

	~Thread();

	//�����߳�
	void start();
	int getId()const;
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;//�����߳�ID
};




//�̳߳�����
class ThreadPool
{
public:
	ThreadPool();//����
	~ThreadPool();//����
	void start(int initThreadSize=std::thread::hardware_concurrency());//�����̳߳�


	//����task�������������ֵ
	void setTaskQueMaxThresHold(int threshold);

	//����cachedģʽ���̵߳�������ֵ
	void setThreadSizeThresHold(int threshold);

	Result submitTask(std::shared_ptr<Task> sp);//new ����
	 
	void setMode(PoolMode mode);


	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:


	//�����̺߳��� 
	void threadFunc(int threadid);//����ʵ�ֵĺ����������̳߳��ṩ��

	bool checkRuningState()const; 

private:
	//std::vector<std::unique_ptr<Thread>> threads_;//�߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;//�߳��б�
	int initThreadSize_;//��ʼ���̵߳�����
	std::atomic_int curThreadSize_;//��¼��ǰ�̳߳��߳�����
	std::atomic_int idleThreadSize;	//��¼�����̵߳�����
	int threadSizeThresHold_;//�߳�������������ֵ

	std::queue<std::shared_ptr<Task>> taskQue_;//������� ��ǿ����ָ��������Ӷ������������ Newһ��
	std::atomic_int taskSize_;//���������
	int taskQueMaxThresHold_;//�����������������ֵ

	std::mutex taskQueMtx_;//��֤������е��̰߳�ȫ
	std::condition_variable notFull_;//��ʾ������в���
	std::condition_variable notEmpty_;//��ʾ������в���
	std::condition_variable exitCond_;//�ȴ��߳���Դȫ������

	PoolMode threadPool_;

	//��ʾ��ǰ�̳߳ص�����״̬
	std::atomic_bool isStart_;


};



#endif // !THREADPOOL_H

