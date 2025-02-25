#include<stdio.h>
#include<chrono>
#include"threadpool.h"
using uLong=unsigned long long;
class MyTask :public Task
{
public:
	MyTask(int begin, int end)
		:begin_(begin),end_(end)
	{}
	Any run()
	{
		uLong sum=0;
		std::cout << "begin threadFunc" << std::this_thread::get_id()<<std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));
		for (int i = begin_; i <= end_; ++i)
		{
			sum += i;
		}
		std::cout << "end threadFunc" << std::this_thread::get_id()<< std::endl;
		return sum;
	}
private:
	int end_;
	int begin_; 
};
int main()
{
	{
		ThreadPool pool;
		pool.setMode(PoolMode::MODE_CACHED);
		pool.start(2);
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		pool.submitTask(std::make_shared<MyTask>(300000001, 400000000));
		pool.submitTask(std::make_shared<MyTask>(400000001, 500000000));
		pool.submitTask(std::make_shared<MyTask>(500000001, 600000000));
		uLong sum1 = res1.get().cast_<uLong>();
		std::cout << sum1 << std::endl;
	}
	std::cout << "main over" << std::endl;
	getchar();
#if 0
	{
		//ThreadPool对象析构后,线程池里面的对象资源应该如何回收
		ThreadPool pool;

		//设置线程池工作模式
		pool.setMode(PoolMode::MODE_CACHED);

		pool.start(4);


		//设计这里的Result机制
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		//

		Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));


		Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

		pool.submitTask(std::make_shared<MyTask>(300000001, 400000000));
		pool.submitTask(std::make_shared<MyTask>(400000001, 500000000));
		pool.submitTask(std::make_shared<MyTask>(500000001, 600000000));
		uLong sum1 = res1.get().cast_<uLong>(); //get返回了一个Any类型,转成具体类型
		uLong sum2 = res2.get().cast_<uLong>(); //get返回了一个Any类型,转成具体类型
		uLong sum3 = res3.get().cast_<uLong>(); //get返回了一个Any类型,转成具体类型
		std::cout << (sum1 + sum2 + sum3) << std::endl;
	}
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());
	//pool.submitTask(std::make_shared<MyTask>());

	//主线程结束 守护线程自动回收
	//std::this_thread::sleep_for(std::chrono::seconds(5));
	getchar();
	return 0;
#endif

}