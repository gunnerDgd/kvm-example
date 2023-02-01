#include <iostream>

#include <linux/kvm.h>
#include <linux/kvm_para.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <unistd.h>

#include <cstring>

const std::uint8_t kvm_exec_code[] = {
        0xba, 0xf8, 0x03, /* mov $0x3f8, %dx */
        0x00, 0xd8,       /* add %bl, %al */
        0x04, '0',        /* add $'0', %al */
        0xee,             /* out %al, (%dx) */
        0xb0, '\n',       /* mov $'\n', %al */
        0xee,             /* out %al, (%dx) */
        0xf4,             /* hlt */
};

int       kvm_result = 0;

int       kvm_handle                ,
          kvm_handle_vm             ,
          kvm_handle_vm_cpu         ,
          kvm_handle_vm_cpu_run_size;
kvm_run*  kvm_handle_vm_cpu_run     ;
kvm_regs  kvm_handle_vm_cpu_reg     ;
kvm_sregs kvm_handle_vm_cpu_sreg    ;

kvm_userspace_memory_region kvm_vm_memory_conf;
void*                       kvm_vm_memory     ;

int main() {
    kvm_handle = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if(kvm_handle <= 0) {
        std::cerr << "[KVM][Fatal] KVM Initialization Failed\n";
        return 1;
    }

    std::cout << "[KVM] KVM Initialized.\n";

    kvm_handle_vm = ioctl(kvm_handle, KVM_CREATE_VM, 0);
    if(kvm_handle_vm <= 0) {
        std::cerr << "[KVM][Fatal] KVM Creation Operation Failed\n";
        return 2;
    }

    std::cout << "[KVM] KVM VM Handle Opened.\n";

    kvm_vm_memory = mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(!kvm_vm_memory) {
        std::cerr << "[KVM][Fatal] KVM User Space Memory Allocation Failed\n";
        return 3;
    }
    std::cout << "[KVM] KVM User Space Memory Allocated (Address : " << kvm_vm_memory << ")\n";

    std::memcpy(kvm_vm_memory, kvm_exec_code, sizeof(kvm_exec_code));
    kvm_vm_memory_conf.slot            = 0;
    kvm_vm_memory_conf.guest_phys_addr = 0x1000;
    kvm_vm_memory_conf.memory_size     = 0x1000;
    kvm_vm_memory_conf.userspace_addr  = (std::uint64_t)kvm_vm_memory;

    kvm_handle_vm_cpu = ioctl(kvm_handle_vm, KVM_CREATE_VCPU, 0);
    if(kvm_handle_vm_cpu < 0) {
        std::cerr << "[KVM][Fatal] KVM Processor Creation Failed (Code : " << errno << ")\n";
        return 4;
    }
    std::cout << "[KVM] KVM Processor Created.\n";

    kvm_result = ioctl(kvm_handle_vm, KVM_SET_USER_MEMORY_REGION, &kvm_vm_memory_conf);
    if(kvm_result) {
        std::cerr << "[KVM][Fatal] User Memory Region Setting Failed (Code : " << errno << ")\n";
        return 5;
    }
    std::cout << "[KVM] User Memory Region Setting Completed\n";

    kvm_handle_vm_cpu_run_size = ioctl(kvm_handle, KVM_GET_VCPU_MMAP_SIZE, 0);
    kvm_handle_vm_cpu_run      = (kvm_run*)mmap (0, kvm_handle_vm_cpu_run_size, PROT_READ | PROT_WRITE, MAP_SHARED, kvm_handle_vm_cpu, 0);

    if(!kvm_handle_vm_cpu_run) {
        std::cerr << "[KVM][Fatal] Process Run Configuration Space Allocated\n";
        return 6;
    }

    kvm_handle_vm_cpu_reg.rip    = 0x1000;
    kvm_handle_vm_cpu_reg.rax    = 3     ;
    kvm_handle_vm_cpu_reg.rbx    = 4     ;
    kvm_handle_vm_cpu_reg.rflags = 0x2   ;

    kvm_result = ioctl(kvm_handle_vm_cpu, KVM_SET_REGS, &kvm_handle_vm_cpu_reg);
    if(kvm_result < 0) {
        std::cerr << "[KVM][Fatal] Processor Register Setting Failed\n";
        return 7;
    }

    kvm_result = ioctl(kvm_handle_vm_cpu, KVM_GET_SREGS, &kvm_handle_vm_cpu_sreg);
    if(kvm_result < 0) {
        std::cerr << "[KVM][Fatal] Failed to get System Register from VCPU.\n";
        return 8;
    }

    kvm_handle_vm_cpu_sreg.cs.base     = 0;
    kvm_handle_vm_cpu_sreg.cs.selector = 0;

    kvm_result = ioctl(kvm_handle_vm_cpu, KVM_SET_SREGS, &kvm_handle_vm_cpu_sreg);
    if(kvm_result < 0) {
        std::cerr << "[KVM][Fatal] Failed to set System Register from VCPU.\n";
        return 8;
    }
    std::cout << "[KVM] System / General Register Setting Completed.\n";

    while (1) {
        ioctl(kvm_handle_vm_cpu, KVM_RUN, NULL);
        switch (kvm_handle_vm_cpu_run->exit_reason) {
            case KVM_EXIT_UNKNOWN:
                std::cout << "[KVM][EXIT] Reason : Unknown\n";
                return 0;
            case KVM_EXIT_HLT:
                std::cout << "[KVM][EXIT] Reason : Halt\n";
                return 0;
            case KVM_EXIT_IO:
                if (kvm_handle_vm_cpu_run->io.direction == KVM_EXIT_IO_OUT &&
                    kvm_handle_vm_cpu_run->io.size      == 1               &&
                    kvm_handle_vm_cpu_run->io.port      == 0x3f8           &&
                    kvm_handle_vm_cpu_run->io.count     == 1) {
                    std::cout << "[KVM][EXIT] Reason : I/O (Result : ";
                        putchar(*(((char *)kvm_handle_vm_cpu_run) + kvm_handle_vm_cpu_run->io.data_offset));
                    std::cout << ")\n";
                }
                break;
        }
    }
}
