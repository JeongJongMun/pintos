#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "include/threads/init.h"
#include "filesys/filesys.h"
#include "userprog/process.h"
#include "include/lib/stdio.h"
#include "include/lib/user/syscall.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface
 * 1. 포인터가 유효하지 않은 경우
 * -> 현재 스택의 상단의 주소가 유저의 가상 주소가 아니면 종료
 * 2. 포인터가 커널 영역에 있는 경우
 * -> 
 * 3. 포인터가 가리키는 블록이 커널 영역에 부분적으로 있는 경우
 * 
 * 커널에서 시스템 호출이 발생 했을 때, 실행된다.
 * 시스템 호출을 처리하기 전에 유효한 주소인지 확인한 후, 시스템 호출이 안전하게 실행될 수 있도록 한다.
 */
void syscall_handler (struct intr_frame *f) {
	check_address(f->rsp);

	uint64_t syscall_num = f->R.rax;

	switch (syscall_num)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		fork(f->R.rdi);
		break;
	case SYS_EXEC:
		exec(f->R.rdi);
		break;
	case SYS_WAIT:
		wait(f->R.rdi);
		break;
	case SYS_CREATE:
		create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		remove(f->R.rdi);
		break;
	case SYS_OPEN:
		open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		filesize(f->R.rdi);
		break;
	case SYS_READ:
		read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_WRITE:
		write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_SEEK:
		seek(f->R.rdi, f->R.rsi);
		break;
	case SYS_TELL:
		tell(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;
	default:
		thread_exit();
		break;
	}
}

/* half - include/threads/init.h 에 선언된 power_off()를 선언하여 핀토스를 종료한다.
 * 교착 상태 등에 대한 일부 정보를 잃게 되므로 거의 사용하지 않는 것이 좋다.
 */
void halt(void) {
	power_off();
}

/* exit - 현재 사용자 프로그램을 종료하여 커널에 status를 반환한다.
 * 프로세스의 부모가 기다리는 경우(아래 참조) 반환되는 상태입니다. 
 * 일반적으로 0 상태는 성공을 나타내고 0이 아닌 값은 오류를 나타냅니다.
 */
void exit(int status) {
	/* Project 2: Process Termination Message */
	struct thread *t = thread_current();
	printf("%s: exit(%d)\n", t->name, t->exit_status);

	/* 프로세스가 종료되는 경우 모든 파일을 암시적으로 닫는다.*/
	int fd = 2;
	// while (t->fdt[fd] != NULL) {
	// 	printf("close fd: %d\n", fd);
	// 	close(fd);
	// 	fd++;
	// }
	thread_exit();
}

pid_t fork(const char *thread_name) {
	printf("fork called ok\n");
	return 0;
}

/* exec - 주어진 인수를 전달하여 현재 프로세스를 cmd_line에 지정된 이름의 실행 파일로 변경합니다. 
 * 성공하면 절대 반환되지 않습니다. 
 * 그렇지 않으면 어떤 이유로든 프로그램을 로드하거나 실행할 수 없는 경우 종료 상태 -1로 프로세스가 종료됩니다. 
 * 이 함수는 실행을 호출한 스레드의 이름을 변경하지 않습니다. 
 * 실행 호출이 진행되는 동안 파일 설명자는 열린 상태로 유지된다는 점에 유의하세요.
 */
int exec(const char *cmd_line) {
	printf("exec called ok\n");
	return 0;
}

/* wait - 자식 프로세스 pid를 기다렸다가 자식의 종료 상태를 검색합니다. 
 * pid가 아직 살아있다면 종료될 때까지 기다립니다. 
 * 그런 다음 pid가 종료하기 위해 전달한 상태를 반환합니다. 
 * pid가 exit()를 호출하지 않았지만 커널에 의해 종료된 경우(예: 예외로 인해 종료된 경우) wait(pid)는 -1을 반환해야 합니다. 
 * 부모 프로세스가 wait를 호출할 때 이미 종료된 자식 프로세스를 기다리는 것은 완전히 합법적이지만, 
 * 커널은 여전히 부모가 자식의 종료 상태를 검색하거나 자식이 커널에 의해 종료되었음을 알 수 있도록 허용해야 합니다.
 * 
 * wait는 다음 조건 중 하나라도 참이면 실패하고 즉시 -1을 반환해야 합니다:
 * 1. 호출 프로세스가 포크에 대한 성공적인 호출에서 반환 값으로 pid를 받은 경우에만 pid는 호출 프로세스의 직접 자식을 참조하지 않습니다. 
 * 	  자식은 상속되지 않습니다. A가 자식 B를 생성하고 B가 자식 프로세스 C를 생성하는 경우, B가 죽었더라도 A는 C를 기다릴 수 없습니다. 
 * 	  프로세스 A의 wait(C) 호출은 실패해야 합니다. 
 * 	  마찬가지로 고아 프로세스는 부모 프로세스가 먼저 종료되면 새 부모에게 할당되지 않습니다.
 * 2. 기다림을 호출하는 프로세스가 이미 pid에서 기다림을 호출했습니다. 
 * 	  즉, 프로세스는 최대 한 번만 특정 자식을 기다릴 수 있습니다.
 */
int wait(pid_t pid) {
	process_wait(pid);
}

/* create - 처음에 initial_size 바이트 크기의 파일이라는 새 파일을 만듭니다. 
 * 성공하면 참을 반환하고, 그렇지 않으면 거짓을 반환합니다. 
 * 새 파일을 만든다고 해서 파일이 열리지는 않습니다. 
 * 새 파일을 열려면 시스템 호출이 필요한 별도의 작업입니다.
 */
bool create(const char *file, unsigned initial_size) {
	check_address(file);
	return filesys_create(file, initial_size);
}

/* remove - file이라는 파일을 삭제합니다. 
 * 성공하면 참을 반환하고, 그렇지 않으면 거짓을 반환합니다. 
 * 파일은 열려 있는지 여부에 관계없이 제거할 수 있으며 열려 있는 파일을 제거해도 닫히지 않습니다. 
 * 자세한 내용은 FAQ에서 열려 있는 파일 제거하기를 참조하세요.
 */
bool remove(const char *file) {
	check_address(file);
	return filesys_remove(file);
}

/* open - file이라는 파일을 연다.
 * fd가 음수가 아닌 정수 핸들을 반환하거나, 파일을 열 수 없는 경우 -1을 반환한다.
 * fd 0/1은 콘솔 용으로 예약되어 있다.
 * fd 0: STDIN_FILENO - 표준 입력
 * fd 1: STDOUT_FILENO - 표준 출력
 * 
 * 이 함수는 fd 0/1 중 어느 것도 반환하지 않으며 시스템 콜 인수로만 사용된다.
 * 
 * 각각의 프로세스는 독립적인 FDT(File Descriptor Table)을 가지며,
 * fd는 자식 프로세스에 의해 상속된다.
 * 
 * 같은 프로세스에서든, 다른 프로세스에서든 단일 파일을 두번 이상 열면
 * 열 때마다 새 fd가 반환된다.
 * 
 * 단일 파일에 대한 서로 다른 fd는 별도의 close()를 통해
 * 독립적으로 닫히며 파일 위치를 공유하지 않는다.
 * 
 * 추가 작업을 수행하려면 0부터 시작하는 정수를 반환하는 Linux 체계를 따라야 한다.
 */
int open(const char *file) {
	check_address(file);
	struct file *file_open = filesys_open(file);
	if (file_open == NULL)
		return -1;

	int fd = add_file_to_fd(file_open);
	if (fd == -1)
		file_close(file_open);

	return fd;
}

/* filesize - fd로 열린 파일의 크기를 바이트 단위로 반환합니다.
 */
int filesize(int fd) {
	struct file *_file = get_file_from_fd(fd);
	
	return file_length(_file);
}

/* read - fd로 열린 파일에서 buffer로 size 바이트를 읽는다.
 * 실제로 읽은 바이트 수(파일 끝에서 0) 또는 
 * 파일을 읽을 수 없는 경우(파일 끝이 아닌 다른 조건으로 인해) -1을 반환한다.
 * fd 0은 input_getc()를 사용하여 키보드에서 읽는다. 
 */
int read(int fd, void *buffer, unsigned size) {
	struct file *_file = get_file_from_fd(fd);
	// TODO: 파일을 읽을 수 없는 경우 -1 반환 처리

	return file_read(_file, buffer, size);
}

/* write - fd로 열린 파일에 buffer에서 size 바이트를 쓴다.
 * 실제로 쓰여진 바이트 수를 반환하며, 일부 바이트가 쓰여지지 않은 경우 크기보다 작을 수 있습니다. 
 * 파일 끝을 지나서 쓰면 일반적으로 파일이 확장되지만 기본 파일 시스템에서는 파일 확장이 구현되지 않습니다. 
 * 예상되는 동작은 파일 끝 부분까지 가능한 한 많은 바이트를 쓰고 실제 쓰여진 바이트 수를 반환하거나 전혀 쓸 수 없는 경우 0을 반환하는 것입니다. 
 * 콘솔에 쓰는 코드는 적어도 크기가 수백 바이트보다 크지 않은 한 putbuf() 호출 한 번으로 모든 버퍼를 써야 합니다(큰 버퍼는 분할하는 것이 합리적입니다). 
 * 그렇지 않으면 다른 프로세스에서 출력한 텍스트 줄이 콘솔에 인터리빙되어 사람이 읽는 사람과 채점 스크립트 모두를 혼란스럽게 만들 수 있습니다.
 */
int write(int fd, const void *buffer, unsigned size) {
	if (fd == STDOUT_FILENO) {
		putbuf(buffer, size);
		return size;
	}
	
	return size;
}

/* seek - 열린 파일 fd에서 읽거나 쓸 다음 바이트를 파일 시작부터 바이트 단위로 표시되는 위치로 변경합니다(따라서 위치가 0이면 파일의 시작입니다). 
 * 파일의 현재 끝을 지나서 찾는 것은 오류가 아닙니다. 
 * 나중에 읽으면 파일 끝을 나타내는 0바이트를 얻습니다. 
 * 나중에 쓰기는 파일을 확장하여 기록되지 않은 간격을 0으로 채웁니다. 
 * (단, 핀토스에서는 프로젝트 4가 완료될 때까지 파일 길이가 고정되어 있으므로 파일 끝을 지나서 쓰면 오류가 반환됩니다.) 
 * 이러한 의미는 파일 시스템에서 구현되며 시스템 호출 구현에 특별한 노력이 필요하지 않습니다.
 */
void seek(int fd, unsigned position) {
	printf("seek called ok\n");
	file_seek(fd, position);
}

/* tell - 열린 파일 fd에서 읽거나 쓸 다음 바이트의 위치를 파일 시작 부분부터 바이트 단위로 반환합니다.
 */
unsigned tell(int fd) {
	printf("tell called ok\n");
	return file_tell(fd);
}

/* close - fd를 닫는다.
 * 프로세스를 exit하거나 terminate하면 
 * 열려 있는 모든 파일 기술자가 닫혀야 한다.
 */
void close(int fd) {
	file_close(get_file_from_fd(fd));
}

/* check_address - 주소가 유효한지 확인한다.
 */
void check_address(uintptr_t addr) {
	if (!is_user_vaddr(addr)) {
		printf("rsp is not in user vaddr\n");
		exit(-1);
	}

	if (KERN_BASE < addr || addr < 0) {
		printf("rsp is in kernel vaddr\n");
		exit(-1);
	}

	if (KERN_BASE < addr + 8 || addr + 8 < 0) {
		printf("rsp partially is in kernel vaddr\n");
		exit(-1);
	}
}

/* add_file_to_fd - file을 fdt에 추가하고 fd를 반환한다.
 */
int add_file_to_fd(struct file *file) {
	struct thread *t = thread_current();
	int fd = 2;
	while (t->fdt[fd] != NULL && fd < 128) {
		fd++;
	}

	printf ("system call!\n");
	thread_exit ();
}
