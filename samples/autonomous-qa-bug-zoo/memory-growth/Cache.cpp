#include <vector>
static std::vector<char*> retained;
void tick(){ retained.push_back(new char[1024 * 1024]); }
