//前几天朋友让帮忙写一个RING3程序来监视TCP包并做数据包分析
//
//本来想HOOK ws2_32!WSASend/Send/WSARecv/Recv，后来发现网上的方法都非常挫，尽是不稳定的HEADER INLINE和修改内存~用SPI之类的，又很麻烦
//
//于是自己写了一种方式实现，非常简单，隐蔽，而且在RING3下应该算是最底层的数据包拦截点了~
//
//目前实现了对HTTP包的过滤和显示~将这个CPP编译成DLL用任意方式注入目标进程，打开DBGVIEW就可以看到目标进程所有的发送和接受的HTTP包了~
//
//
//下面是代码

#include "stdafx.h"
#include "windows.h"
#include "winnt.h"

PVOID pNtDeviceIoControl  = NULL ; 
//

#define AFD_RECV 0x12017

#define AFD_SEND 0x1201f


typedef struct AFD_WSABUF{
    UINT  len ;
    PCHAR  buf ;
}AFD_WSABUF , *PAFD_WSABUF;

typedef struct AFD_INFO {
    PAFD_WSABUF  BufferArray ; 
    ULONG  BufferCount ; 
    ULONG  AfdFlags ;
    ULONG  TdiFlags ;
} AFD_INFO,  *PAFD_INFO;
typedef LONG NTSTATUS;

#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)

const CHAR GetXX[] = "GET ";
const CHAR PostXX[] = "POST ";
const CHAR HttpXX[] = "HTTP";
//////////////////////////////////////////////////////////////////////////
//
// LookupSendPacket
// 检查Send包
// 目前实现了过滤HTTP请求（GET AND POST）
//
//////////////////////////////////////////////////////////////////////////

BOOL LookupSendPacket(PVOID Buffer , ULONG Len)
{
    if (Len < 5)
    {
        return FALSE ; 
    }

    //外层已有异常捕获

    if (memcmp(Buffer , GetXX , 4) == 0 
        ||
        memcmp(Buffer , PostXX , 5) == 0 )
    {
        return TRUE ; 
    }
    return FALSE ; 
}  
//////////////////////////////////////////////////////////////////////////
//
// LookupRecvPacket
//
// 检查Recv包
// 在这里可以实现Recv包查字典功能
// 目前实现了过滤HTTP返回数据包的功能
//
//
///////////////////////////////////////////////////////////////////////////
BOOL LookupRecvPacket(PVOID Buffer , ULONG Len)
{
    if (Len < 4)
    {
        return FALSE ; 
    }

    if (memcmp(Buffer , HttpXX , 4) == 0 )
    {
        return TRUE ; 
    }

    return FALSE ; 
}
//hook函数

//////////////////////////////////////////////////////////////////////////
//
// NtDeviceIoControlFile的HOOK函数 
// ws2_32.dll的send , recv最终会调用到mswsock.dll内的数据发送函数
// mswsock.dll会调用NtDeviceIoControlFile向TDI Client驱动发送Send Recv指令
// 我们在这里做拦截，可以过滤所有的TCP 收发包（UDP之类亦可，不过要更改指令）
//
//////////////////////////////////////////////////////////////////////////

NTSTATUS __stdcall NewNtDeviceIoControlFile(
    HANDLE FileHandle,
    HANDLE Event OPTIONAL,
    PVOID ApcRoutine OPTIONAL,
    PVOID ApcContext OPTIONAL,
    PVOID IoStatusBlock,
    ULONG IoControlCode,
    PVOID InputBuffer OPTIONAL,
    ULONG InputBufferLength,
    PVOID OutputBuffer OPTIONAL,
    ULONG OutputBufferLength
    )
{

    //先调用原始函数

    LONG stat ; 
    __asm
    {
        push  OutputBufferLength
        push  OutputBuffer
        push  InputBufferLength
        push  InputBuffer 
        push  IoControlCode
        push  IoStatusBlock 
        push  ApcContext
        push  ApcRoutine
        push  Event
        push  FileHandle
        call  pNtDeviceIoControl
        mov    stat ,eax
    }

    //如果原始函数失败了（例如RECV无数据）

    if (!NT_SUCCESS(stat))
    {
        return stat ; 
    }

    //检查是否为TCP收发指令

    if (IoControlCode != AFD_SEND && IoControlCode != AFD_RECV)
    {
        return stat ; 
    }


    //访问AFD INFO结构，获得SEND或RECV的BUFFER信息
    //这里可能是有问题的BUFFER，因此我们要加TRY EXCEPT
    //

    __try
    {
        //从InputBuffer得到Buffer和Len

        PAFD_INFO AfdInfo = (PAFD_INFO)InputBuffer ; 
        PVOID Buffer = AfdInfo->BufferArray->buf ; 
        ULONG Len = AfdInfo->BufferArray->len;

        if (IoControlCode == AFD_SEND)
        {
            if (LookupSendPacket(Buffer , Len))
            {
                //输出包内容
                //这里输出调试信息，可以用DbgView查看，如果有UI可以做成SendMessage形式~
                OutputDebugString("SendPacket!\n");    
                OutputDebugString((char*)Buffer);
            }
        }
        else
        {
            if (LookupRecvPacket(Buffer , Len))
            {
                OutputDebugString("RecvPacket!\n");
                OutputDebugString((char*)Buffer);
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        return stat ; 
    }

    return stat ; 






}


//////////////////////////////////////////////////////////////////////////
//
//  Hook mswsock.dll导出表的Ntdll!NtDeviceIoControlFile
//  并过滤其对TDI Cilent的请求来过滤封包
//  稳定，隐蔽，RING3下最底层的包过滤~
//
//////////////////////////////////////////////////////////////////////////
void SuperHookDeviceIoControl()
{
    //得到ws2_32.dll的模块基址
    HMODULE hMod = LoadLibrary("mswsock.dll");
    if (hMod == 0 )
    {
        return ;
    }

    //得到DOS头

    PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)hMod ; 

    //如果DOS头无效
    if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return ; 
    }

    //得到NT头

    PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)((ULONG)hMod + pDosHeader->e_lfanew);

    //如果NT头无效
    if (pNtHeaders->Signature != IMAGE_NT_SIGNATURE)
    {
        return ; 
    }

    //检查输入表数据目录是否存在
    if (pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress == 0 ||
        pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size == 0 )
    {
        return ; 
    }
    //得到输入表描述指针

    PIMAGE_IMPORT_DESCRIPTOR ImportDescriptor = (PIMAGE_IMPORT_DESCRIPTOR)((ULONG)hMod + pNtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

    PIMAGE_THUNK_DATA ThunkData ; 

    //检查每个输入项
    while(ImportDescriptor->FirstThunk)
    {
        //检查输入表项是否为ntdll.dll

        char* dllname = (char*)((ULONG)hMod + ImportDescriptor->Name);

        //如果不是，则跳到下一个处理

        if (stricmp(dllname , "ntdll.dll") !=0)
        {
            ImportDescriptor ++ ; 
            continue;
        }

        ThunkData = (PIMAGE_THUNK_DATA)((ULONG)hMod + ImportDescriptor->OriginalFirstThunk);

        int no = 1;
        while(ThunkData->u1.Function)
        {
            //检查函数是否为NtDeviceIoControlFile

            char* functionname = (char*)((ULONG)hMod + ThunkData->u1.AddressOfData + 2);
            if (stricmp(functionname , "NtDeviceIoControlFile") == 0 )
            {
                //
                //如果是，那么记录原始函数地址
                //HOOK我们的函数地址
                //
                ULONG myaddr = (ULONG)NewNtDeviceIoControlFile;
                ULONG btw ; 
                PDWORD lpAddr = (DWORD *)((ULONG)hMod + (DWORD)ImportDescriptor->FirstThunk) +(no-1);
                pNtDeviceIoControl = (PVOID)(*(ULONG*)lpAddr) ; 
                WriteProcessMemory(GetCurrentProcess() , lpAddr , &myaddr , sizeof(ULONG), &btw );
                return ; 

            }

            no++;
            ThunkData ++;
        }
        ImportDescriptor ++;
    }
    return ; 
}

//////////////////////////////////////////////////////////////////////////
//
// CheckProcess 检查是否是需要挂钩的进程
//
//
//////////////////////////////////////////////////////////////////////////

BOOL CheckProcess()
{
    //在此加入你的进程过滤
    return TRUE ;
}

BOOL APIENTRY DllMain( HANDLE hModule, 
    DWORD  ul_reason_for_call, 
    LPVOID lpReserved
    )
{
    //当加载DLL时，进行API HOOK

    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {  
        //检查是否是要过滤的进程
        if (CheckProcess() == FALSE)
        {  
            //如果不是，返回FALSE,将自身从进程中卸除
            return FALSE ; 
        }

        //HOOK API
        SuperHookDeviceIoControl();
    }
    return TRUE;
} 