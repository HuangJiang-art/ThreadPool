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
#include<future>

const int TASK_MAX_THRESHOLD = 2;//INT32_MAX;
const int THREAD_MAX_THRESHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60;//��λ:s
//�߳�����

class Thread
{
public:
	//�̺߳�����������
	using ThreadFunc = std::function<void(int)>;

	Thread(ThreadFunc func)
		:func_(func)
		, threadId_(generateId_++)
	{}

	~Thread() = default;

	//�����߳�
	void start()
	{
		//����һ���߳���ִ��һ���̺߳���
		std::thread t(func_, threadId_);//C++11 �̶߳���t ���̺߳���func_ 
		t.detach(); //���÷����߳�
	}
	int getId()const
	{
		return threadId_;
	}
private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;//�����߳�ID
};
int Thread::generateId_ = 0;

enum class PoolMode//ʹ��ʱҪ����������
{
	MODE_FIXED,
	MODE_CACHED,
};


//�̳߳�����
class ThreadPool
{
public:
	ThreadPool()
	:initThreadSize_(0)
	, taskSize_(0)
	, taskQueMaxThresHold_(TASK_MAX_THRESHOLD)
	, threadSizeThresHold_(THREAD_MAX_THRESHOLD)
	, threadPool_(PoolMode::MODE_FIXED)
	, isStart_(false)
	, idleThreadSize(0)
	, curThreadSize_(0)
	{}//����
	~ThreadPool()
	{
		isStart_ = false;

		//wait all thread return  ����&����ִ������
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		notEmpty_.notify_all();
		exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });//ÿ���߳̽���ʱҪnotify �������߳����������
	}//����
	void start(int initThreadSize = std::thread::hardware_concurrency())
	{
		//��������״̬
		isStart_ = true;

		//��¼��ʼ�̸߳���
		initThreadSize_ = initThreadSize;
		curThreadSize_ = initThreadSize;
		//�����̶߳���

		for (int i = 0; i < initThreadSize_; i++)
		{
			//����thread�����ʱ�򣬰��̺߳�������thread����
			std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));//���������Ǵ�������ʱ��Ҫ�Ĳ���
			//threads_.emplace_back(std::move(ptr));
			int threadid = ptr->getId();
			threads_.emplace(threadid, std::move(ptr));
		}

		//���������߳�
		//std::vector<Thread*> threads_;//�߳��б�
		for (int i = 0; i < initThreadSize_; i++)
		{
			threads_[i]->start();//��Ҫȥִ��һ���̺߳���
			idleThreadSize++;//��¼��ʼ�����̵߳�����
		}
	}//�����̳߳�


	//����task�������������ֵ
	void setTaskQueMaxThresHold(int threshold)
	{
		if (checkRuningState())//����Ѿ�������ֱ�ӷ���
		{
			return;
		}
		taskQueMaxThresHold_ = threshold;
	}

	//����cachedģʽ���̵߳�������ֵ
	void setThreadSizeThresHold(int threshold)
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

	//ʹ�ÿɱ��ģ���̣���submitTask���Խ������������������������Ĳ���
	//����ֵfuture
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		// ������񣬷��������������
		using RType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));//�������� ����Ϊ()����ֵΪRType
		std::future<RType> result = task->get_future();

		// ��ȡ��
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		// �û��ύ�����������������1s�������ж��ύ����ʧ�ܣ�����
		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]()->bool { return taskQue_.size() < (size_t)taskQueMaxThresHold_; }))
		{
			// ��ʾnotFull_�ȴ�1s�֣�������Ȼû������
			std::cerr << "task queue is full, submit task fail." << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType { return RType(); });
			(*task)();//ʧ��Ҳ�����ִ�� ��Ȼ������
			return task->get_future();
		}

		// ����п��࣬������������������
		// taskQue_.emplace(sp);  
		// using Task = std::function<void()>;
		taskQue_.emplace([task]() {(*task)(); });
		taskSize_++;

		// ��Ϊ�·�������������п϶������ˣ���notEmpty_�Ͻ���֪ͨ���Ͽ�����߳�ִ������
		notEmpty_.notify_all();

		// cachedģʽ ������ȽϽ��� ������С��������� ��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��̳߳���
		if (threadPool_ == PoolMode::MODE_CACHED
			&& taskSize_ > idleThreadSize
			&& curThreadSize_ < threadSizeThresHold_)
		{
			std::cout << ">>> create new thread..." << std::endl;

			// �����µ��̶߳���
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getId();
			threads_.emplace(threadId, std::move(ptr));
			// �����߳�
			threads_[threadId]->start();
			// �޸��̸߳�����صı���
			curThreadSize_++;
			idleThreadSize++;
		}

		// ���������Result����
		return result;
	}

	void setMode(PoolMode mode)
	{
		if (checkRuningState())//����Ѿ�������ֱ�ӷ���
		{
			return;
		}
		threadPool_ = mode;
	}


	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:


	//�����̺߳��� 
	void threadFunc(int threadid)//����ʵ�ֵĺ����������̳߳��ṩ��
	{
		auto lastTime = std::chrono::high_resolution_clock().now();//����һ��forѭ����ı���
		//�̳߳�����������Ҫ��������ܽ���
		for (;;)
		{
			//std::shared_ptr<Task> task;
			Task task;
			{
				//�Ȼ�ȡ��
				std::unique_lock<std::mutex> lock(taskQueMtx_);

				std::cout << "���Ի�ȡ����..." << std::this_thread::get_id() << std::endl;

				//cahchedģʽ�� ���ܴ����˺ܶ��̣߳�������ʱ�䳬��60s��Ӧ�ðѶ�����߳̽�������(����initThreadSize_�������߳�Ҫ����)
				while (taskQue_.size() == 0)//�������������û������ʱ����
				{
					if (!isStart_)//�̳߳ؽ��� ������Դ
					{
						//��ִ����������
						threads_.erase(threadid);//ɾ���̶߳���
						//curThreadSize_--;
						//idleThreadSize--;
						std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
						exitCond_.notify_all();
						return;//�̺߳�������
					}
					if (threadPool_ == PoolMode::MODE_CACHED)
					{
						//������������ʱ������
						if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
						{
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
							{
								//��ʼ���յ�ǰ�߳�
								//��¼�߳���������ر�����ֵ�޸�
								//���̶߳�����߳��б�������ɾ��
								threads_.erase(threadid);//ɾ���̶߳���
								curThreadSize_--;
								idleThreadSize--;
								std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
								return;
							}
						}
					}
					else
					{
						//�ȴ�notEmpty
						notEmpty_.wait(lock);
					}
				}
				//���������ȡ
				idleThreadSize--;//�߳�������һ

				std::cout << "��ȡ����ɹ�..." << std::this_thread::get_id() << std::endl;
				//�����������ȡһ���������
				task = taskQue_.front();
				taskQue_.pop();
				taskSize_--;

				//�����Ȼ��ʣ�����񣬼���֪ͨ�������߳�ִ������
				if (taskQue_.size() > 0)
				{
					notEmpty_.notify_all();
				}

				//ȡ��һ������ ֪ͨ�ȴ�����������
				notFull_.notify_all();
			}

			//��ǰ�̸߳���ִ���������
			if (task != nullptr)
			{
				//task->exec();
				//ִ�����������֪ͨ
				task();//ִ��function<void()>
			}
			idleThreadSize++;//�������
			lastTime = std::chrono::high_resolution_clock().now(); //�����߳�ִ���������ʱ��
		}
	}

	bool checkRuningState()const
	{
		return isStart_;
	}

private:
	//std::vector<std::unique_ptr<Thread>> threads_;//�߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;//�߳��б�
	int initThreadSize_;//��ʼ���̵߳�����
	std::atomic_int curThreadSize_;//��¼��ǰ�̳߳��߳�����
	std::atomic_int idleThreadSize;	//��¼�����̵߳�����
	int threadSizeThresHold_;//�߳�������������ֵ

	//Task����=��������
	using Task = std::function<void()>;
	std::queue<Task> taskQue_;//������� ��ǿ����ָ��������Ӷ������������ Newһ��
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



//#ifndef THREADPOOL_H
//#define THREADPOOL_H
//
//#include <iostream>
//#include <vector>
//#include <queue>
//#include <memory>
//#include <atomic>
//#include <mutex>
//#include <condition_variable>
//#include <functional>
//#include <unordered_map>
//#include <thread>
//#include <future>
//
//const int TASK_MAX_THRESHHOLD = 2; // INT32_MAX;
//const int THREAD_MAX_THRESHHOLD = 1024;
//const int THREAD_MAX_IDLE_TIME = 60; // ��λ����
//
//
//// �̳߳�֧�ֵ�ģʽ
//enum class PoolMode
//{
//	MODE_FIXED,  // �̶��������߳�
//	MODE_CACHED, // �߳������ɶ�̬����
//};
//
//// �߳�����
//class Thread
//{
//public:
//	// �̺߳�����������
//	using ThreadFunc = std::function<void(int)>;
//
//	// �̹߳���
//	Thread(ThreadFunc func)
//		: func_(func)
//		, threadId_(generateId_++)
//	{}
//	// �߳�����
//	~Thread() = default;
//
//	// �����߳�
//	void start()
//	{
//		// ����һ���߳���ִ��һ���̺߳��� pthread_create
//		std::thread t(func_, threadId_);  // C++11��˵ �̶߳���t  ���̺߳���func_
//		t.detach(); // ���÷����߳�   pthread_detach  pthread_t���óɷ����߳�
//	}
//
//	// ��ȡ�߳�id
//	int getId()const
//	{
//		return threadId_;
//	}
//private:
//	ThreadFunc func_;
//	static int generateId_;
//	int threadId_;  // �����߳�id
//};
//
//int Thread::generateId_ = 0;
//
//// �̳߳�����
//class ThreadPool
//{
//public:
//	// �̳߳ع���
//	ThreadPool()
//		: initThreadSize_(0)
//		, taskSize_(0)
//		, idleThreadSize_(0)
//		, curThreadSize_(0)
//		, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
//		, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
//		, poolMode_(PoolMode::MODE_FIXED)
//		, isPoolRunning_(false)
//	{}
//
//	// �̳߳�����
//	~ThreadPool()
//	{
//		isPoolRunning_ = false;
//
//		// �ȴ��̳߳��������е��̷߳���  ������״̬������ & ����ִ��������
//		std::unique_lock<std::mutex> lock(taskQueMtx_);
//		notEmpty_.notify_all();
//		exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
//	}
//
//	// �����̳߳صĹ���ģʽ
//	void setMode(PoolMode mode)
//	{
//		if (checkRunningState())
//			return;
//		poolMode_ = mode;
//	}
//
//	// ����task�������������ֵ
//	void setTaskQueMaxThreshHold(int threshhold)
//	{
//		if (checkRunningState())
//			return;
//		taskQueMaxThreshHold_ = threshhold;
//	}
//
//	// �����̳߳�cachedģʽ���߳���ֵ
//	void setThreadSizeThreshHold(int threshhold)
//	{
//		if (checkRunningState())
//			return;
//		if (poolMode_ == PoolMode::MODE_CACHED)
//		{
//			threadSizeThreshHold_ = threshhold;
//		}
//	}
//
//	// ���̳߳��ύ����
//	// ʹ�ÿɱ��ģ���̣���submitTask���Խ������������������������Ĳ���
//	// pool.submitTask(sum1, 10, 20);   csdn  ���ؿ���  ��ֵ����+�����۵�ԭ��
//	// ����ֵfuture<>
//	template<typename Func, typename... Args>
//	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
//	{
//		// ������񣬷��������������
//		using RType = decltype(func(args...));
//		auto task = std::make_shared<std::packaged_task<RType()>>(
//			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
//		std::future<RType> result = task->get_future();
//
//		// ��ȡ��
//		std::unique_lock<std::mutex> lock(taskQueMtx_);
//		// �û��ύ�����������������1s�������ж��ύ����ʧ�ܣ�����
//		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
//			[&]()->bool { return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
//		{
//			// ��ʾnotFull_�ȴ�1s�֣�������Ȼû������
//			std::cerr << "task queue is full, submit task fail." << std::endl;
//			auto task = std::make_shared<std::packaged_task<RType()>>(
//				[]()->RType { return RType(); });
//			(*task)();//ʧ��Ҳ�����ִ�� ��Ȼ������
//			return task->get_future();
//		}
//
//		// ����п��࣬������������������
//		// taskQue_.emplace(sp);  
//		// using Task = std::function<void()>;
//		taskQue_.emplace([task]() {(*task)(); });
//		taskSize_++;
//
//		// ��Ϊ�·�������������п϶������ˣ���notEmpty_�Ͻ���֪ͨ���Ͽ�����߳�ִ������
//		notEmpty_.notify_all();
//
//		// cachedģʽ ������ȽϽ��� ������С��������� ��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ�����µ��̳߳���
//		if (poolMode_ == PoolMode::MODE_CACHED
//			&& taskSize_ > idleThreadSize_
//			&& curThreadSize_ < threadSizeThreshHold_)
//		{
//			std::cout << ">>> create new thread..." << std::endl;
//
//			// �����µ��̶߳���
//			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
//			int threadId = ptr->getId();
//			threads_.emplace(threadId, std::move(ptr));
//			// �����߳�
//			threads_[threadId]->start();
//			// �޸��̸߳�����صı���
//			curThreadSize_++;
//			idleThreadSize_++;
//		}
//
//		// ���������Result����
//		return result;
//	}
//
//	// �����̳߳�
//	void start(int initThreadSize = std::thread::hardware_concurrency())
//	{
//		// �����̳߳ص�����״̬
//		isPoolRunning_ = true;
//
//		// ��¼��ʼ�̸߳���
//		initThreadSize_ = initThreadSize;
//		curThreadSize_ = initThreadSize;
//
//		// �����̶߳���
//		for (int i = 0; i < initThreadSize_; i++)
//		{
//			// ����thread�̶߳����ʱ�򣬰��̺߳�������thread�̶߳���
//			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
//			int threadId = ptr->getId();
//			threads_.emplace(threadId, std::move(ptr));
//			// threads_.emplace_back(std::move(ptr));
//		}
//
//		// ���������߳�  std::vector<Thread*> threads_;
//		for (int i = 0; i < initThreadSize_; i++)
//		{
//			threads_[i]->start(); // ��Ҫȥִ��һ���̺߳���
//			idleThreadSize_++;    // ��¼��ʼ�����̵߳�����
//		}
//	}
//
//	ThreadPool(const ThreadPool&) = delete;
//	ThreadPool& operator=(const ThreadPool&) = delete;
//
//private:
//	// �����̺߳���
//	void threadFunc(int threadid)
//	{
//		auto lastTime = std::chrono::high_resolution_clock().now();
//
//		// �����������ִ����ɣ��̳߳زſ��Ի��������߳���Դ
//		for (;;)
//		{
//			Task task;
//			{
//				// �Ȼ�ȡ��
//				std::unique_lock<std::mutex> lock(taskQueMtx_);
//
//				std::cout << "tid:" << std::this_thread::get_id()
//					<< "���Ի�ȡ����..." << std::endl;
//
//				// cachedģʽ�£��п����Ѿ������˺ܶ���̣߳����ǿ���ʱ�䳬��60s��Ӧ�ðѶ�����߳�
//				// �������յ�������initThreadSize_�������߳�Ҫ���л��գ�
//				// ��ǰʱ�� - ��һ���߳�ִ�е�ʱ�� > 60s
//
//				// ÿһ���з���һ��   ��ô���֣���ʱ���أ������������ִ�з���
//				// �� + ˫���ж�
//				while (taskQue_.size() == 0)
//				{
//					// �̳߳�Ҫ�����������߳���Դ
//					if (!isPoolRunning_)
//					{
//						threads_.erase(threadid); // std::this_thread::getid()
//						std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
//							<< std::endl;
//						exitCond_.notify_all();
//						return; // �̺߳����������߳̽���
//					}
//
//					if (poolMode_ == PoolMode::MODE_CACHED)
//					{
//						// ������������ʱ������
//						if (std::cv_status::timeout ==
//							notEmpty_.wait_for(lock, std::chrono::seconds(1)))
//						{
//							auto now = std::chrono::high_resolution_clock().now();
//							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
//							if (dur.count() >= THREAD_MAX_IDLE_TIME
//								&& curThreadSize_ > initThreadSize_)
//							{
//								// ��ʼ���յ�ǰ�߳�
//								// ��¼�߳���������ر�����ֵ�޸�
//								// ���̶߳�����߳��б�������ɾ��   û�а취 threadFunc��=��thread����
//								// threadid => thread���� => ɾ��
//								threads_.erase(threadid); // std::this_thread::getid()
//								curThreadSize_--;
//								idleThreadSize_--;
//
//								std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
//									<< std::endl;
//								return;
//							}
//						}
//					}
//					else
//					{
//						// �ȴ�notEmpty����
//						notEmpty_.wait(lock);
//					}
//				}
//
//				idleThreadSize_--;
//
//				std::cout << "tid:" << std::this_thread::get_id()
//					<< "��ȡ����ɹ�..." << std::endl;
//
//				// �����������ȡһ���������
//				task = taskQue_.front();
//				taskQue_.pop();
//				taskSize_--;
//
//				// �����Ȼ��ʣ�����񣬼���֪ͨ�������߳�ִ������
//				if (taskQue_.size() > 0)
//				{
//					notEmpty_.notify_all();
//				}
//
//				// ȡ��һ�����񣬽���֪ͨ��֪ͨ���Լ����ύ��������
//				notFull_.notify_all();
//			} // ��Ӧ�ð����ͷŵ�
//
//			// ��ǰ�̸߳���ִ���������
//			if (task != nullptr)
//			{
//				task(); // ִ��function<void()> 
//			}
//
//			idleThreadSize_++;
//			lastTime = std::chrono::high_resolution_clock().now(); // �����߳�ִ���������ʱ��
//		}
//	}
//
//	// ���pool������״̬
//	bool checkRunningState() const
//	{
//		return isPoolRunning_;
//	}
//
//private:
//	std::unordered_map<int, std::unique_ptr<Thread>> threads_; // �߳��б�
//
//	int initThreadSize_;  // ��ʼ���߳�����
//	int threadSizeThreshHold_; // �߳�����������ֵ
//	std::atomic_int curThreadSize_;	// ��¼��ǰ�̳߳������̵߳�������
//	std::atomic_int idleThreadSize_; // ��¼�����̵߳�����
//
//	// Task���� =�� ��������
//	using Task = std::function<void()>;
//	std::queue<Task> taskQue_; // �������
//	std::atomic_int taskSize_; // ���������
//	int taskQueMaxThreshHold_;  // �����������������ֵ
//
//	std::mutex taskQueMtx_; // ��֤������е��̰߳�ȫ
//	std::condition_variable notFull_; // ��ʾ������в���
//	std::condition_variable notEmpty_; // ��ʾ������в���
//	std::condition_variable exitCond_; // �ȵ��߳���Դȫ������
//
//	PoolMode poolMode_; // ��ǰ�̳߳صĹ���ģʽ
//	std::atomic_bool isPoolRunning_; // ��ʾ��ǰ�̳߳ص�����״̬
//};
//
//#endif
//
