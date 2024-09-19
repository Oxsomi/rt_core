
struct Nicu {
	float a, b;
	uint64_t c;
	uint16_t d;
	float16_t e;
	int16_t f;
};

RWStructuredBuffer<Nicu> _sbuffer0;

[[oxc::stage("compute")]]
[[oxc::extension("16BitTypes", "I64")]]
[numthreads(1, 1, 1)]
void main(uint id : SV_DispatchThreadID) {
	_sbuffer0[id].a = 123;
}