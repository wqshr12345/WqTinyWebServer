#ifndef TIME_WHEEL_TIMER
#define TIME_WHEEL_TIMER

#include<time.h>
#include<netinet/in.h>
#include<stdio.h>

#define BUFFER_SIZE 64
class tw_timer;

//结构体，绑定一个连接socket的全部信息(包括ip地址、sockID、定时器等)
struct client_data{
    sockaddr_in address;
    int sockfd;
    char buf[BUFFER_SIZE];
    tw_timer* timer;
};

//定时器类
class tw_timer{
public:
    tw_timer(int rot,int ts):next(NULL),prev(NULL),rotation(rot),time_slot(ts){}
public:
    int rotation;//记录定时器在时间轮转多少圈后生效
    int time_slot;//记录定时器属于时间轮上的哪个槽
    void (*cb_func)(client_data*);//定时器回调函数
    client_data* user_data;//客户数据
    tw_timer* next;
    tw_timer* prev;
};

class time_wheel{
    public:
	//构造函数，初始化槽。
	time_wheel():cur_slot(0){
	    for(int i = 0;i<N;++i){
		slots[i] = NULL;
	    }
	}
	//析构函数，删除每个槽里面的定时器。不用删槽，这个留给析构函数体运行结束后，会自行删除的。因为定时器是new出来的，需要显式delete。
	~time_wheel(){
	    for(int i = 0;i<N;++i){
		tw_timer* tmp = slots[i];
		while(tmp){
		    slots[i] = tmp->next;
		    delete tmp;
		    tmp = slots[i];
		}
	    }

	}
	//根据timeout创建定时器，并插入槽中。
	tw_timer* add_timer(int timeout){
	    if(timeout<0){
		return NULL;
	    }
	    int ticks = 0;//ticks表明这个定时器在时间轮转动多少滴答后执行。
	    if(timeout<SI){
		ticks = 1;
	    }
	    //为什么是向下取整呢？
	    else{
		ticks = timeout/SI;
	    }
	    int rotation = ticks/N;
	    int ts = (cur_slot+(ticks%N))%N;
	    tw_timer* timer = new tw_timer(rotation,ts);
	    //如果第ts个槽为空
	    if(!slots[ts]){
		printf("add timer,rotation is %d,ts is %d,cur_slot is%d\n",rotation,ts,cur_slot);
		slots[ts] = timer;
	    }
	    else{
		timer->next = slots[ts];
		slots[ts]->prev = timer;
		slots[ts] = timer;
	    }
	    return timer;
	}
	void del_timer(tw_timer* timer){
	    if(!timer){
		return;
	    }
	    int ts = timer->time_slot;
	    //如果删的是头部
	    if(timer == slots[ts]){
		slots[ts] = slots[ts]->next;
		if(slots[ts]){
		    slots[ts]->prev = NULL;
		}
		delete timer;
	    }
	    else{
		timer->prev->next = timer->next;
		if(timer->next){
		    timer->next->prev = timer->prev;
		}
		delete timer;
	    }
	}
	void tick(){
	    tw_timer* tmp = slots[cur_slot];
	    printf("current slot is %d\n",cur_slot);
	    while(tmp){
		printf("tick the timer once\n");
		//如果定时器的rotation>0,说明在这一轮不起作用
		if(tmp->rotation>0){
		    tmp->rotation--;
		    tmp = tmp->next;
		}
		else{
		    tmp->cb_func(tmp->user_data);
		    if(tmp==slots[cur_slot]){
			pritnf("delete header in cur_slot\n");
			sots[cur_slot] = tmp->next;
			delete tmp;
			if(slots[cur_slot]){
			    slots[cur_slot]->prev = NULL;
			}
			tmp = slots[cur_slot];
		    }
		    else{
			tmp->prev->next = tmp->next;
			if(tmp->next){
			    tmp->next->prev = tmp->prev;
			}
			tw_timer* tmp2 = tmp->next;
			delete tmp;
			tmp = tmp2;
		    }
		}
	    }
	cur_slot = ++cuurslot%N;
	}
    private:
	static const int N = 60;//槽的数目
	static const int SI = 1;//时间轮转动周期
	tw_timer* slots[N];//时间轮的槽
	int cur_slot;//时间轮的当前槽
};
