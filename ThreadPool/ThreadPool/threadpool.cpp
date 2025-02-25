#include"threadpool.h"

const int TASK_MAX_THRESHOLD =	INT32_MAX;
const int THREAD_MAX_THRESHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60;//��λ:s

//�̳߳ع���
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

//�̳߳�����
ThreadPool::~ThreadPool()
{
	isStart_ = false;

	//wait all thread return  ����&����ִ������
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });//ÿ���߳̽���ʱҪnotify �������߳����������
}
void ThreadPool::setMode(PoolMode mode) //�����̳߳ع���ģʽ
{
	if (checkRuningState())//����Ѿ�������ֱ�ӷ���
	{
		return;
	}
	threadPool_ = mode; 
}

void ThreadPool::setTaskQueMaxThresHold(int threshold)//����Task�������������ֵ
{
	if (checkRuningState())//����Ѿ�������ֱ�ӷ���
	{
		return;
	}
	taskQueMaxThresHold_ = threshold;
}

void ThreadPool::setThreadSizeThresHold(int threshold)//����cachedģʽ���̵߳�������ֵ
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

Result ThreadPool::submitTask(std::shared_ptr<Task> sp)//���̳߳��ύ����
{
	//��ȡ��
	std::unique_lock<std::mutex> lock(taskQueMtx_);
    
	//�߳�ͨ��  �ȴ���������п���
	//while (taskQue_.size() == taskQueMaxThresHold_)
	//{
	//	notFull_.wait(lock);
	//}
	if (!notFull_.wait_for(lock, std::chrono::seconds(1), [&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThresHold_; }))
	{
		//��ʾnotFull_�ȴ�1s�ӣ�������Ȼû������
		std::cerr << "task queue is full,submit task fail." << std::endl;
		return Result(sp, false);
	}
	//����֪ͨ ��������
	//�û��ύ��������ܳ���1s�������ж��ύʧ�ܣ�����  

	//����п��࣬��������������б�
	taskQue_.emplace(sp);
	taskSize_++;

	// ��notEmpty_�Ͻ���֪ͨ �Ͽ�����߳�ִ������
	notEmpty_.notify_all();

	//cachedģʽ:��������������Ϳ����̵߳������ж��Ƿ���Ҫ�����µ��̳߳���->С���������
	if (threadPool_ == PoolMode::MODE_CACHED&&taskSize_>idleThreadSize&&curThreadSize_<threadSizeThresHold_)
	{
		std::cout << "create new thread!" << std::endl;
		//�������߳�
		std::unique_ptr<Thread> ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));//���������Ǵ�������ʱ��Ҫ�Ĳ���
		int threadid = ptr->getId();
		threads_.emplace(threadid,std::move(ptr));
		threads_[threadid]->start();//�����߳�
		idleThreadSize++;
		curThreadSize_++;
	}
	return Result(sp);
}

void ThreadPool::start(int initThreadSize)//�����̳߳�
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
}

//�����̺߳��� ���������������������
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();//����һ��forѭ����ı���
	//�̳߳�����������Ҫ��������ܽ���
	for(;;)
	{
		std::shared_ptr<Task> task;
		{
			//�Ȼ�ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "���Ի�ȡ����..." << std::this_thread::get_id()<<std::endl;

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
							std::cout << "threadid:" << std::this_thread::get_id() <<"exit"<< std::endl;
							return;
						}
					}
				}
				else
				{
					//�ȴ�notEmpty
					notEmpty_.wait(lock);
				}
				//if (!isStart_)//�̳߳�Ҫ������Դ
				//{
				//	//������������߳�
				//	threads_.erase(threadid);//ɾ���̶߳���
				//	//curThreadSize_--;
				//	//idleThreadSize--;
				//	std::cout << "threadid:" << std::this_thread::get_id() << "exit" << std::endl;
				//	exitCond_.notify_all();
				//	return;
				//}
			}
			//���������ȡ
			idleThreadSize--;//�߳�������һ

			std::cout << "��ȡ����ɹ�..." << std::this_thread::get_id()<< std::endl;
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
			task->exec();
			//ִ�����������֪ͨ

		}
		idleThreadSize++;//�������
		lastTime = std::chrono::high_resolution_clock().now(); //�����߳�ִ���������ʱ��
	}
}

bool ThreadPool::checkRuningState()const
{
	return isStart_;
}
//�̷߳���ʵ��
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
	//����һ���߳���ִ��һ���̺߳���
	std::thread t(func_,threadId_);//C++11 �̶߳���t ���̺߳���func_ 
	t.detach(); //���÷����߳�
}
int  Thread::getId()const
{
	return threadId_;
}

//������
Task::Task()
	:result_(nullptr)
{}
void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run());//���﷢����̬����
	}
}
void Task::setResult(Result* res)
{
	result_ = res;
}


//////����ֵ���������
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
	sem_.wait();//taskûִ���� ������
	return std::move(any_);//ֻ������ֵ���п�������

}
void Result::setVal(Any any)
{
	//�洢task�ķ���ֵ
	this->any_ = std::move(any);
	sem_.post();
}