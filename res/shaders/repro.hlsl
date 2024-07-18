
struct ColorPayload {
	float16_t4 color;
};

[shader("miss")]
[extension("16BitTypes")]
void mainMiss(inout ColorPayload payload) {
	payload.color = float16_t4(WorldRayDirection() * 0.5 + 0.5, 1);
}
