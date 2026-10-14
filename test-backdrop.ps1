# A native, solid-color underlay. Its class brush also handles cross-process
# background invalidation; no WinForms repaint scheduling is involved.
Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;
public sealed class NotesBackdrop:IDisposable{
 public IntPtr Handle{get;private set;}IntPtr instance,brush;string name;
 delegate IntPtr Procedure(IntPtr h,uint m,IntPtr w,IntPtr l);Procedure callback;
 [StructLayout(LayoutKind.Sequential)]struct Rect{public int left,top,right,bottom;}
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]struct Class{public uint style;public IntPtr proc;public int extra,windowExtra;public IntPtr instance,icon,cursor,brush;public string menu,name;}
 [StructLayout(LayoutKind.Sequential)]struct Message{public IntPtr window;public uint message;public UIntPtr w;public IntPtr l;public uint time;public int x,y;public uint reserved;}
 [DllImport("user32.dll")]static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]static extern IntPtr GetModuleHandle(string name);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern ushort RegisterClass(ref Class c);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern bool UnregisterClass(string name,IntPtr instance);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr CreateWindowEx(uint ex,string cls,string title,uint style,int x,int y,int w,int height,IntPtr parent,IntPtr menu,IntPtr instance,IntPtr data);
 [DllImport("user32.dll")]static extern IntPtr DefWindowProc(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]static extern bool SystemParametersInfo(uint action,uint param,ref Rect r,uint flags);
 [DllImport("user32.dll")]static extern bool SetWindowPos(IntPtr h,IntPtr order,int x,int y,int w,int height,uint flags);
 [DllImport("user32.dll")]static extern bool DestroyWindow(IntPtr h);
 [DllImport("user32.dll")]static extern bool RedrawWindow(IntPtr h,IntPtr rect,IntPtr region,uint flags);
 [DllImport("user32.dll")]static extern bool PeekMessage(out Message m,IntPtr h,uint first,uint last,uint flags);
 [DllImport("user32.dll")]static extern bool TranslateMessage(ref Message m);
 [DllImport("user32.dll")]static extern IntPtr DispatchMessage(ref Message m);
 [DllImport("gdi32.dll")]static extern IntPtr CreateSolidBrush(uint color);
 [DllImport("gdi32.dll")]static extern bool DeleteObject(IntPtr h);
 public NotesBackdrop(uint color){
  SetThreadDpiAwarenessContext((IntPtr)(-4));instance=GetModuleHandle(null);name="LightNotesBackdrop-"+Guid.NewGuid();brush=CreateSolidBrush(color);
  callback=(h,m,w,l)=>DefWindowProc(h,m,w,l);
  var cls=new Class{style=3,proc=Marshal.GetFunctionPointerForDelegate(callback),instance=instance,brush=brush,name=name};
  if(RegisterClass(ref cls)==0)throw new Exception("Cannot register test backdrop");
  var r=new Rect();SystemParametersInfo(48,0,ref r,0);
  Handle=CreateWindowEx(0x08000080,name,"LightNotes test underlay",0x80000000,r.left,r.top,r.right-r.left,r.bottom-r.top,IntPtr.Zero,IntPtr.Zero,instance,IntPtr.Zero);
  if(Handle==IntPtr.Zero)throw new Exception("Cannot create test backdrop");
 }
 public void Show(){SetWindowPos(Handle,(IntPtr)(-1),0,0,0,0,0x53);Refresh();}
 public void Refresh(){RedrawWindow(Handle,IntPtr.Zero,IntPtr.Zero,0x105);}
 public void Pump(){Message m;while(PeekMessage(out m,IntPtr.Zero,0,0,1)){TranslateMessage(ref m);DispatchMessage(ref m);}}
 public void Dispose(){if(Handle!=IntPtr.Zero){DestroyWindow(Handle);Handle=IntPtr.Zero;}UnregisterClass(name,instance);if(brush!=IntPtr.Zero){DeleteObject(brush);brush=IntPtr.Zero;}GC.KeepAlive(callback);}
}
'@
