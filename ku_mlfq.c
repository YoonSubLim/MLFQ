#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdlib.h>

struct timeval tpend;
struct timeval tpstart;

typedef struct PCB{
    int processNum; // 프로세스번호 : 알파벳 지정시
    int prio; // 현재 우선순위값
    pid_t pid; // 프로세스 ID
    time_t spent_time; // 사용시간
}PCB;

typedef struct Node{
    PCB* pcb; // process control block
    struct Node* next; // 다음 노드
}Node;

typedef struct priorQueue{
    Node* front; // 첫 노드
    Node* rear; // 끝 노드
    int count;
}priorQueue;

time_t S = 0; // prior boost when S = 10
Node* runningProcNode = NULL; // 현재 실행 중인 프로세스 노드
int sliceCount = 0; // 사용한 time slice
int ts; // timeslice. 부모 프로세스에서 초기화해주어 자식 프로세스

// 프로세스 노드 큐 배열. 0 high, 2 low
priorQueue priorQ[3]; // 0 high, 2 low

// 큐 초기화
void initQueue(priorQueue* que){
    que->front = que->rear= NULL;
    que->count = 0;
}

// 큐 비어있다면 true
int isEmpty(priorQueue* que){
    return (que->count==0);
}

// 확인용 // 제출시 사용되지 않음.
// void printQueue(){
//     Node* curNode;

//     for(int i=0; i<3; i++){
//         printf("Queue %d : ", i);
//         if(!isEmpty(&(priorQ[i]))){
//             curNode = priorQ[i].front;
//             while(curNode != NULL){
//                 printf("%d ", curNode->pcb->processNum);
//                 curNode = curNode->next;
//             }
//         }
//         printf("\n");
//     }
// }

// 프로세스 노드 큐에, PCB 정보를 토대로 새로운 노드 생성하여 enqueue
void enqueue(priorQueue* que, PCB* pcb){
    Node* cur = (Node*)malloc(sizeof(Node));
    cur->pcb = pcb;
    cur->next = NULL;

    if(isEmpty(que)){
        que->front = cur;
    }else{
        (que->rear)->next = cur;
    }
    que->rear = cur;
    (que->count)++;
}

// dequeue 시 dequeue한 노드의 PCB 정보 리턴.
PCB* dequeue(priorQueue* que){
    Node *now;
    PCB* delPCB;
    if(isEmpty(que)){
        return NULL;
    }
    now = que->front;
    delPCB = now->pcb;
    que->front = now->next;
    free(now);
    (que->count)--;
    return delPCB;
}

void priorBoost(priorQueue* que){
    PCB* movePCB;
    int i;

    // 1, 2번큐 -> 0번 큐로 일괄 이동 // 0번 큐 기존 프로세스는 time 유지
    for(i=1; i<=2; i++){
        if(!isEmpty(&(que[i]))){ // 비어있지 않으면
            while(!isEmpty(&(que[i]))){ // 빌 때까지
                movePCB = dequeue(&(que[i]));
                movePCB->spent_time = 0;
                movePCB->prio = 0;
                enqueue(&(que[0]), movePCB);
            }
        }
    }

}

// 우선순위 감소. 노드 dequeue하고 해당 PCB를 하위 큐에 새로운 노드로 enqueue한다.
void reducePrior(Node* reduceNode){
    PCB* movePCB = reduceNode->pcb;
    int prio = movePCB->prio;
    ((reduceNode->pcb)->prio)++;
    dequeue(&(priorQ[prio]));
    prio++;
    enqueue(&(priorQ[prio]), movePCB);
    movePCB->spent_time = 0;
}

// 큐 배열에 들어있는 모든 (자식)프로세스들에 종료 시그널을 보낸다.
void terminate(){
    int i;
    Node* termNode;
    pid_t termPid;
    Node* curNode;
    for(i=0; i<3; i++){
        if(!isEmpty(&(priorQ[i]))){
            curNode = priorQ[i].front;
            while(curNode != NULL){
                termNode = curNode;
                termPid = termNode->pcb->pid;
                curNode = curNode->next;
                free(termNode->pcb);
                free(termNode);
                kill(termPid, SIGKILL);
            }
        }
    }
}

void schedFunc(){

    Node* nextRunNode; // 다음 동작할 프로세스의 노드

    if(runningProcNode != NULL){ // 돌던 프로세스 있을 경우, 즉 처음 시작할 때 외 경우
        time_t timedif;
        gettimeofday(&tpend, NULL); // 종료 시간
        kill((runningProcNode->pcb)->pid, SIGSTOP); // 돌던 프로세스 stop
        
        timedif = tpend.tv_sec - tpstart.tv_sec; // 이전 핸들러 끝나면서 저장한 시작 값과의 차이를 구한다
        S += timedif;
        (runningProcNode->pcb)->spent_time += timedif;
        sliceCount++;
        //printf("S : %ld timedif : %ld spent_time : %ld sliceCount : %d\n", S, timedif, (runningProcNode->pcb)->spent_time, sliceCount);
        // timeslice 다 쓰면 종료
        if(sliceCount == ts){
            terminate();
            return;
        }
        // reduce 혹은 prior boost가 필요한 경우
        if(((runningProcNode->pcb)->spent_time >= 2 && (runningProcNode->pcb)->prio < 2) || S >= 10){
            // 둘 다 해당되는 경우 reduce 수행 후 boost
            if((runningProcNode->pcb)->spent_time >= 2 && (runningProcNode->pcb)->prio < 2){
                reducePrior(runningProcNode);
                int i;
                for(i=0; i<3; i++){
                    if(!isEmpty(&(priorQ[i]))){
                        nextRunNode = priorQ[i].front;
                        break;
                    }
                }
            }
            if(S >= 10){
                S = 0;
                priorBoost(priorQ);
                nextRunNode = priorQ[0].front;
            }
        }else{ // reduce / boost 안하는 경우
            // 다음 돌 노드를 지정하고, NULL이면 상위 우선순위 front부터 다시 RR
            nextRunNode = runningProcNode->next;
            if(nextRunNode == NULL){
                int i;
                for(i=0; i<3; i++){
                    if(!isEmpty(&(priorQ[i]))){
                        nextRunNode = priorQ[i].front;
                        break;
                    }
                }
            }
        }
        //printf("[%d] 작동 후 큐 \n", sliceCount);
        //printQueue();
    }
    else{ // 돌던 프로세스가 NULL, 즉 처음.
        nextRunNode = priorQ[0].front;
    }

    gettimeofday(&tpstart, NULL); // 시작 시간
    runningProcNode = nextRunNode; // 돌고 있는 프로세스 변경
    kill((nextRunNode->pcb)->pid, SIGCONT); // 깨운다
}

// 자식 프로세스 끝날 때까지 대기
pid_t r_wait(int *stat_loc){
    int retval;

    while(((retval = wait(stat_loc))==-1 && (errno == EINTR)));
    return retval;
}

int main(int argc, char *argv[]){

    pid_t childpid = 0;
	int n; // 프로세스 개수
    int i;
    int procNum = 1; // 프로세스 번호
    PCB* curPCB;

	// 인자 수 오입력시 Error Handling
	if(argc != 3){
	    fprintf(stderr, "arg error\n");
	    return 1;
	}
    
	// String 값을 int 값으로 변환하여 저장
	n = atoi(argv[1]); // 프로세스 개수
	ts = atoi(argv[2]); // time slice 개수

    // 범위 벗어난 값 입력시 Error Handling
    if(n < 1 || n > 26 || ts <= 0){
        fprintf(stderr, "arg value error\n");
        return 1;
    }
    
    // 큐 초기화
    for(i=0; i<3; i++){
        initQueue(&(priorQ[i]));
    }

    // timer에 넘겨줄 itimerval 설정. 타이머 간격은 1초. 초기 설정값은 1ms.
    struct itimerval itmr;
    itmr.it_interval.tv_sec = 1;
    itmr.it_interval.tv_usec = 0;
    itmr.it_value.tv_sec = 0;
    itmr.it_value.tv_usec = 1;

    // signal handler를 schedFunc으로 지정.
    struct sigaction newact;
    newact.sa_handler = schedFunc;
    newact.sa_flags = 0;
    if((sigemptyset(&newact.sa_flags) == -1)||(sigaction(SIGALRM, &newact, NULL)== -1)){
        perror("handler 등록 실패\n");
    }
    
    for(i=1; i<=n; i++){
        if((childpid=fork()) == 0){ // 자식 프로세스라면 프로세스 번호로 
            char arg1[2] = "\0";
            arg1[0] = 'A' + procNum - 1;
            execl("./ku_app", "ku_app", arg1, (char*)0);
            printf("실행되면 안되는 구간\n");
            break;
        }else if(childpid > 0){ // 부모프로세스에서 매 fork마다 최상위 큐에 PCB 만들어 enqueue.
            curPCB = (PCB*)malloc(sizeof(PCB));
            curPCB->pid = childpid;
            curPCB->spent_time = 0;
            curPCB->prio = 0;
            curPCB->processNum = procNum;
            enqueue(&(priorQ[0]), curPCB);
        }else if(childpid < 0){ // fork 실패시
            perror("Failed to fork. Retry.");
            i--;
            continue;
        }
        procNum++; // 매 fork 마다 지정해줄 프로세스 번호 증가.
    }

    // 부모 sleep. 깨어나면 모든 자식 프로세스 생성되어 자는 중.
    sleep(5);

    // 타이머 설정. 설정 후 곧바로(1ms) 핸들러 진입.
    if(setitimer(ITIMER_REAL, &itmr, (struct itimerval*)NULL)==0){}

    // 부모는 자식 프로세스가 모두 끝나기를 기다린다.
    while(r_wait(NULL)>0);
    //printf("\n 부모 끝\n");
    return 0;
}