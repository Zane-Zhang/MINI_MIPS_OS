#include "lib.h"
#include <mmu.h>
#include <env.h>
#include <kerelf.h>

#define debug 0
#define TMPPAGE		(BY2PG)
#define TMPPAGETOP	(TMPPAGE+BY2PG)

int
init_stack(u_int child, char **argv, u_int *init_esp)
{
	int argc, i, r, tot;
	char *strings;
	u_int *args;

	// Count the number of arguments (argc)
	// and the total amount of space needed for strings (tot)
	tot = 0;
	for (argc=0; argv[argc]; argc++)
		tot += strlen(argv[argc])+1;

	// Make sure everything will fit in the initial stack page
	if (ROUND(tot, 4)+4*(argc+3) > BY2PG)
		return -E_NO_MEM;

	// Determine where to place the strings and the args array
	strings = (char*)TMPPAGETOP - tot;
	args = (u_int*)(TMPPAGETOP - ROUND(tot, 4) - 4*(argc+1));

	if ((r = syscall_mem_alloc(0, TMPPAGE, PTE_V|PTE_R)) < 0)
		return r;
	// Replace this with your code to:
	//
	//	- copy the argument strings into the stack page at 'strings'
	char *ctemp,*argv_temp;
	u_int j;
	ctemp = strings;
	for(i = 0;i < argc; i++)
	{
		argv_temp = argv[i];
		for(j=0;j < strlen(argv[i]);j++)
		{
			*ctemp = *argv_temp;
			ctemp++;
			argv_temp++;
		}
		*ctemp = 0;
		ctemp++;
	}
	//	- initialize args[0..argc-1] to be pointers to these strings
	//	  that will be valid addresses for the child environment
	//	  (for whom this page will be at USTACKTOP-BY2PG!).
	ctemp = (char *)(USTACKTOP - TMPPAGETOP + (u_int)strings);
	for(i = 0;i < argc;i++)
	{
		args[i] = (u_int)ctemp;
		ctemp += strlen(argv[i])+1;
	}
	//	- set args[argc] to 0 to null-terminate the args array.
	ctemp--;
	args[argc] = ctemp;
	//	- push two more words onto the child's stack below 'args',
	//	  containing the argc and argv parameters to be passed
	//	  to the child's umain() function.
	u_int *pargv_ptr;
	pargv_ptr = args - 1;
	*pargv_ptr = USTACKTOP - TMPPAGETOP + (u_int)args;
	pargv_ptr--;
	*pargv_ptr = argc;
	//
	//	- set *init_esp to the initial stack pointer for the child
	//
	*init_esp = USTACKTOP - TMPPAGETOP + (u_int)pargv_ptr;
//	*init_esp = USTACKTOP;	// Change this!

	if ((r = syscall_mem_map(0, TMPPAGE, child, USTACKTOP-BY2PG, PTE_V|PTE_R)) < 0)
		goto error;
	if ((r = syscall_mem_unmap(0, TMPPAGE)) < 0)
		goto error;

	return 0;

error:
	syscall_mem_unmap(0, TMPPAGE);
	return r;
}

int usr_is_elf_format(u_char *binary){
	Elf32_Ehdr *ehdr = (Elf32_Ehdr *)binary;
	if (ehdr->e_ident[0] == ELFMAG0 &&
        ehdr->e_ident[1] == ELFMAG1 &&
        ehdr->e_ident[2] == ELFMAG2 &&
        ehdr->e_ident[3] == ELFMAG3) {
        return 1;
    }   

    return 0;
}

int 
usr_load_elf(int fd , Elf32_Phdr *ph, int child_envid){

	//Hint: maybe this function is useful 
	//      If you want to use this func, you should fill it ,it's not hard
	return 0;
}
int fuck_elf(int fd,int child_envid){
	int psize=0;
	u_char* binary;
	binary=fd2data(num2fd(fd));
	Elf32_Ehdr *ehdr = (Elf32_Ehdr *)binary;
	Elf32_Phdr *phdr = NULL;
	u_char *ptr_ph_table = NULL;
        Elf32_Half ph_entry_count;
        Elf32_Half ph_entry_size;
	u_int* blk;
        int r;
	int i,range,tmp;
	tmp=0x60000000-BY2PG;
	if ( !usr_is_elf_format(binary)) {
		writef("not elf!");
                return -1;
        }
	 ptr_ph_table = binary + ehdr->e_phoff;
        ph_entry_count = ehdr->e_phnum;
        ph_entry_size = ehdr->e_phentsize;

	while (ph_entry_count--) {
                phdr = (Elf32_Phdr *)ptr_ph_table;

                if (phdr->p_type == PT_LOAD) {
			//writef("filesz%x,memsz%x\n",phdr->p_filesz,phdr->p_memsz);
			for(i=0;i<phdr->p_memsz;i+=BY2PG){
				r=read_map(fd,i+0x1000,&blk);
				if(r<0){
					writef("spawn:error at rad_map\n");
					return r;
				}
				//syscall_mem_map(0,blk,child_envid,i+0x400000,PTE_V|PTE_R);
				syscall_mem_alloc(0,tmp,PTE_V|PTE_R);
				user_bzero(tmp,BY2PG);
				if((phdr->p_filesz-i<BY2PG)&&(int)(phdr->p_filesz)-i>0){
					range=phdr->p_filesz-i;
					//writef("cut%x,range:%x\n",i,range);
					user_bcopy(blk,tmp,range);
	
				}
				else if((int)(phdr->p_filesz)-i>0){
					range=BY2PG;
					//writef("full:%x,%x\n",i,phdr->p_filesz);
					user_bcopy(blk,tmp,range);

				}
			/*	else{
					user_bzero(tmp,BY2PG);
					writef("zero at %x\n",i);
				}*/
				//user_bcopy(blk,tmp,range);
				syscall_mem_map(0,tmp,child_envid,i+0x400000,PTE_V|PTE_R);
				syscall_mem_unmap(0,tmp);
			}
			
                }

                ptr_ph_table += ph_entry_size;
        }

               return 0;


}
int spawn(char *prog, char **argv)
{
	u_char elfbuf[512];
	int r;
	int fd;
	u_int child_envid;
	int size, text_start;
	u_int i, *blk;
	u_int esp;
	Elf32_Ehdr* elf;
	Elf32_Phdr* ph;
	// Note 0: some variable may be not used,you can cancel them as you like
	// Step 1: Open the file specified by `prog` (prog is the path of the program)
	if((fd=open(prog, O_RDONLY))<0){
		user_panic("spawn ::open line 102 RDONLY wrong !\n");
		return r;
	}
	// Your code begins here
	// Before Step 2 , You had better check the "target" spawned is a execute bin
	// Step 2: Allocate an env (Hint: using syscall_env_alloc())

	child_envid=syscall_env_alloc();
	if(child_envid<0){
		writef("spawn:error at allocing a env\n");
		return child_envid;
	}
	
	// Step 3: Using init_stack(...) to initialize the stack of the allocated env
	init_stack(child_envid,argv,&esp);
	// Step 3: Map file's content to new env's text segment
	size=((struct Filefd*)num2fd(fd))->f_file.f_size;
/*
	text_start=0x400000;
	for(i=0x1000;i<size;i+=BY2PG){
		r=read_map(fd,i,&blk);
		if(r<0){
			writef("sapwn:error at rad_map\n");
			return r;
		}
		syscall_mem_map(0,blk,child_envid,text_start,PTE_V|PTE_R);
		text_start+=BY2PG;
	}
	*/
	fuck_elf(fd,child_envid);

	//        Hint 1: what is the offset of the text segment in file? try to use objdump to find out.
	//        Hint 2: using read_map(...)
	//		  Hint 3: Important!!! sometimes ,its not safe to use read_map ,guess why 
	//				  If you understand, you can achieve the "load APP" with any method
	// Note1: Step 1 and 2 need sanity check. In other words, you should check whether
	//       the file is opened successfully, and env is allocated successfully.
	// Note2: You can achieve this func in any way ，remember to ensure the correctness
	//        Maybe you can review lab3 
	// Your code ends here

	struct Trapframe *tf;
	writef("\n::::::::::spawn size : %x  sp : %x::::::::\n",size,esp);
	tf = &(envs[ENVX(child_envid)].env_tf);
	tf->pc = UTEXT;
	tf->regs[29]=esp;


	// Share memory
	u_int pdeno = 0;
	u_int pteno = 0;
	u_int pn = 0;
	u_int va = 0;
	for(pdeno = 0;pdeno<PDX(UTOP);pdeno++)
	{
		if(!((* vpd)[pdeno]&PTE_V))
			continue;
		for(pteno = 0;pteno<=PTX(~0);pteno++)
		{
			pn = (pdeno<<10)+pteno;
			if(((* vpt)[pn]&PTE_V)&&((* vpt)[pn]&PTE_LIBRARY))
			{
				va = pn*BY2PG;

				if((r = syscall_mem_map(0,va,child_envid,va,(PTE_V|PTE_R|PTE_LIBRARY)))<0)
				{

					writef("va: %x   child_envid: %x   \n",va,child_envid);
					user_panic("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@");
					return r;
				}
			}
		}
	}


	if((r = syscall_set_env_status(child_envid, ENV_RUNNABLE)) < 0)
	{
		writef("set child runnable is wrong\n");
		return r;
	}
	return child_envid;		

}

int
spawnl(char *prog, char *args, ...)
{
	return spawn(prog, &args);
}


