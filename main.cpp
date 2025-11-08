#include <ncurses.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cctype>

// ---- structures ----
struct CpuTot { unsigned long long u=0,n=0,s=0,i=0,io=0,irq=0,sirq=0,st=0; };
struct Row {
    int pid=0; std::string cmd; double pcpu=0.0, pmem=0.0; unsigned long rss_kb=0;
};

// ---- helpers ----
static bool isnum(const std::string& s){
    if(s.empty()) return false;
    for(char c: s) if(!std::isdigit((unsigned char)c)) return false;
    return true;
}

// read aggregate CPU times from /proc/stat
static CpuTot readCpu(){
    std::ifstream f("/proc/stat"); std::string tag; CpuTot c;
    if(f>>tag && tag=="cpu") f>>c.u>>c.n>>c.s>>c.i>>c.io>>c.irq>>c.sirq>>c.st;
    return c;
}
static inline unsigned long long tot(const CpuTot& c){ return c.u+c.n+c.s+c.i+c.io+c.irq+c.sirq+c.st; }
static inline unsigned long long idle(const CpuTot& c){ return c.i+c.io; }

// memory (used/total) in kB from /proc/meminfo
static std::pair<unsigned long long,unsigned long long> memUsedTotalKB(){
    std::ifstream f("/proc/meminfo"); std::string k,u; unsigned long long v, T=0, A=0;
    while(f>>k>>v>>u){ if(k=="MemTotal:") T=v; else if(k=="MemAvailable:") A=v; }
    unsigned long long used=(A<=T)?(T-A):0; return {used,T};
}

// list numeric PID directories in /proc
static std::vector<int> pids(){
    std::vector<int> v; DIR* d=opendir("/proc"); if(!d) return v; dirent* e;
    while((e=readdir(d))) if(e->d_type==DT_DIR && isnum(e->d_name)) v.push_back(std::stoi(e->d_name));
    closedir(d); return v;
}

// read utime+stime ticks, rss pages, and comm from /proc/[pid]/stat
static bool readStat(int pid, unsigned long long& ticks, unsigned long& rssp, std::string& comm){
    std::ifstream f("/proc/"+std::to_string(pid)+"/stat"); if(!f) return false; std::string line; std::getline(f,line);
    size_t l=line.find('('), r=line.rfind(')'); if(l==std::string::npos||r==std::string::npos||r<=l) return false;
    comm=line.substr(l+1,r-l-1);
    std::string rest=line.substr(r+2); std::istringstream ss(rest);
    std::vector<std::string> v; std::string t; while(ss>>t) v.push_back(t);
    if(v.size()<24) return false;
    unsigned long long ut=std::stoull(v[13-1]), st=std::stoull(v[14-1]);
    ticks=ut+st; rssp=std::stoul(v[23-1]); return true;
}

// best-effort command name (cmdline first, then comm)
static std::string cmdName(int pid){
    std::ifstream f1("/proc/"+std::to_string(pid)+"/cmdline"); std::string s; std::getline(f1,s,'\0');
    if(!s.empty()){ size_t p=s.find_last_of('/'); if(p!=std::string::npos) s=s.substr(p+1); return s; }
    std::ifstream f2("/proc/"+std::to_string(pid)+"/comm"); std::getline(f2,s); return s;
}

int main(){
    // ---- Day 1: TUI + gather data ----
    initscr(); cbreak(); noecho(); keypad(stdscr,TRUE); nodelay(stdscr,TRUE); curs_set(0);

    const long page_kb = sysconf(_SC_PAGESIZE)/1024;
    int refresh_ms = 2000;     // ---- Day 5: real-time refresh interval (ms)

    // ---- Day 3: sorting controls ----
    // 1 = CPU, 2 = MEM, 0 = PID ; toggle order with 'r'
    int sortKey = 1; bool desc = true;
    bool top5Only = false;     // toggle with 't' (start with full list)

    // baselines for CPU delta and per-PID %CPU
    CpuTot prevC = readCpu(); unsigned long long prevTot=tot(prevC), prevIdle=idle(prevC);
    std::map<int,unsigned long long> prevTicks;
    for(int pid: pids()){ unsigned long long t=0; unsigned long r=0; std::string c; if(readStat(pid,t,r,c)) prevTicks[pid]=t; }

    while(true){
        // system CPU% and memory
        CpuTot nowC = readCpu(); unsigned long long nowTot=tot(nowC), nowIdle=idle(nowC);
        double totald = (nowTot>prevTot)? double(nowTot-prevTot):0.0;
        double idled  = (nowIdle>prevIdle)? double(nowIdle-prevIdle):0.0;
        double cpuPct = (totald>0)? 100.0*((totald-idled)/totald):0.0;

        auto [usedKB,totalKB] = memUsedTotalKB();
        double memPct = totalKB? 100.0*double(usedKB)/double(totalKB):0.0;

        // ---- Day 2: process list with %CPU & %MEM ----
        std::vector<Row> rows;
        for(int pid: pids()){
            unsigned long long t=0; unsigned long rssp=0; std::string comm;
            if(!readStat(pid,t,rssp,comm)) continue;

            double pcpu=0.0; auto it=prevTicks.find(pid);
            if(it!=prevTicks.end() && totald>0){ double d=double(t-it->second); pcpu=100.0*(d/totald); if(pcpu<0) pcpu=0; }

            unsigned long rss_kb=rssp*page_kb;
            double pmem = totalKB? 100.0*double(rss_kb)/double(totalKB):0.0;

            Row r; r.pid=pid; r.cmd=cmdName(pid); if(r.cmd.empty()) r.cmd=comm;
            r.pcpu=pcpu; r.pmem=pmem; r.rss_kb=rss_kb;
            rows.push_back(r);
        }

        // ---- Day 3: sorting by CPU/MEM (and PID) ----
        auto cmp=[&](const Row&a,const Row&b){
            auto better=[&](auto A, auto B){ return desc? A>B : A<B; };
            if(sortKey==1) return better(a.pcpu,b.pcpu);
            if(sortKey==2) return better(a.pmem,b.pmem);
            return better(a.pid,b.pid);
        };
        std::sort(rows.begin(), rows.end(), cmp);
        if(top5Only && rows.size()>5) rows.resize(5);

        // ---- draw UI ----
        erase();
        mvprintw(0,0,"System Monitor | CPU:%5.1f%%  Mem:%5.1f%% (%llu/%llu MB) | sort:%s%s | t:top5/all  c/m/p:sort  r:rev  k:kill  q:quit",
                 cpuPct, memPct,
                 (unsigned long long)(usedKB/1024), (unsigned long long)(totalKB/1024),
                 (sortKey==1?"CPU":(sortKey==2?"MEM":"PID")), (desc?"↓":"↑"));
        mvprintw(2,0,"%-6s %-18s %6s %6s %6s","PID","COMMAND","%CPU","%MEM","RSSMB");

        int y=3, maxy,maxx; getmaxyx(stdscr,maxy,maxx);
        for(const auto& r: rows){
            if(y>=maxy-1) break;
            std::string name=r.cmd; if((int)name.size()>18) name=name.substr(0,18);
            mvprintw(y++,0,"%-6d %-18s %6.2f %6.2f %6.1f", r.pid, name.c_str(), r.pcpu, r.pmem, r.rss_kb/1024.0);
        }
        mvprintw(maxy-1,0,"View: %s  Refresh: %d ms", top5Only?"Top-5":"All", refresh_ms);
        refresh();

        // ---- keys ----
        int ch=getch();
        if(ch=='q'||ch=='Q'){ endwin(); return 0; }
        else if(ch=='t'||ch=='T'){ top5Only=!top5Only; }
        else if(ch=='c'||ch=='C'){ sortKey=1; desc=true; }
        else if(ch=='m'||ch=='M'){ sortKey=2; desc=true; }
        else if(ch=='p'||ch=='P'){ sortKey=0; desc=false; }
        else if(ch=='r'||ch=='R'){ desc=!desc; }
        else if(ch=='k'||ch=='K'){     // ---- Day 4: kill process ----
            echo(); nodelay(stdscr,FALSE); curs_set(1);
            mvprintw(maxy-2,0,"Enter PID to kill (empty=cancel): "); clrtoeol();
            char buf[32]; getnstr(buf,31);
            noecho(); nodelay(stdscr,TRUE); curs_set(0);
            std::string s(buf);
            if(!s.empty() && std::all_of(s.begin(),s.end(),::isdigit)){
                int tpid=std::stoi(s);
                if(tpid!=1){ // never kill init
                    if(kill(tpid,SIGTERM)==0){ usleep(200000); if(kill(tpid,0)==0) kill(tpid,SIGKILL); }
                }
            }
        }

        // update baselines for next interval (Day 5)
        prevC=nowC; prevTot=nowTot; prevIdle=nowIdle;
        prevTicks.clear();
        for(int pid: pids()){ unsigned long long t=0; unsigned long r=0; std::string c; if(readStat(pid,t,r,c)) prevTicks[pid]=t; }

        // sleep with key break (keeps UI responsive)
        int waited=0; const int step=50;
        while(waited<refresh_ms){ usleep(step*1000); int c=getch(); if(c!=ERR){ ungetch(c); break; } waited+=step; }
    }
}
