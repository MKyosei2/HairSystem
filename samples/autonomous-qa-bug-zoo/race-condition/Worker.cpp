#include <thread>
int counter = 0;
void run(){ std::thread a([]{ for(int i=0;i<100000;i++) counter++; }); std::thread b([]{ for(int i=0;i<100000;i++) counter++; }); a.join(); b.join(); }
