var MIN_SPEED = 0.01;
var MAX_SPEED = 20.0;
var MAX_GRAVITY = 40.0;
var BALL_COUNT = 3;
var MIDI_CHANNEL = 1;

var params = {
	gravity: 0.0,
	bumperForce: 0.5
};
var balls = [ ];

function cube(x) { return x * x * x; }
function clamp(x, min, max) { return (x < min ? min : (x > max ? max : x)); }
function floorMod(x, y) { return ((x % y) + y) % y; }

function tick(sampleFrames, sampleRate) {
	var rsr16 = 16.0 / sampleRate;

	var gravity = cube(params.gravity) * MAX_GRAVITY;
	var bumperForce = params.bumperForce * 2.0;
	var eventCount = 0;
	var offset = 0;
	var events = [ ];
	while (offset < sampleFrames) {
		for (var i = 0; i < BALL_COUNT; ++i) {
			var ball = balls[i];
			if (ball.active) {
				var nx = ball.x + ball.dx * rsr16;
				var ny = ball.y + ball.dy * rsr16;
				var d = nx * nx + ny * ny;
				
				if (d < 1.0) {
					ball.x = nx;
					ball.y = ny;
					ball.dy += gravity * rsr16;
				} else {
					var dx = ball.dx;
					var dy = ball.dy;
					ball.dx = (dx * ny * ny - dx * nx * nx - 2 * nx * ny * dy) / d;
					ball.dy = (dy * nx * nx - dy * ny * ny - 2 * nx * ny * dx) / d;
					var a = Math.atan2(ball.y, ball.x);
					var v = Math.sqrt(ball.dx * ball.dx + ball.dy * ball.dy);
					var midiVel = 0;
					if (v > 0.00001) {
						midiVel = clamp(Math.round(50.0 * Math.log(v) + 127.0), 0, 127);
					}
					if (midiVel > 0) {
						var octave = 4 + (i + 1) % 3;
						var note = floorMod((Math.round(a * (12.0 / (2.0 * Math.PI)) + 9.0)), 12) + octave * 12;
						var evt = {
							offset: offset,
							status: 0x90 | MIDI_CHANNEL,
							data1: note,
							data2: midiVel
						};
						events.push(evt);
						// evt.offset = offset + duration;
						// evt.status = 0x80 | midiChannel;
						// noteOffs.push_back(evt);
					}
					if (v * bumperForce < MIN_SPEED) {
						ball.dx = 0.0;
						ball.dy = 0.0;
					} else if (v * bumperForce > MAX_SPEED) {
						ball.dx *= MAX_SPEED / v;
						ball.dy *= MAX_SPEED / v;
					} else {
						ball.dx *= bumperForce;
						ball.dy *= bumperForce;
					}
				}
			}
		}
		offset += 16;
	}
	/*
	while (eventCount < maxEvents && !noteOffs.empty() && noteOffs.front().offset < sampleFrames) {
		events[eventCount] = noteOffs.front();
		++eventCount;
		noteOffs.pop_front();
	}
	for (std::deque<MidiEvent>::iterator it = noteOffs.begin(); it != noteOffs.end(); ++it) {
		it->offset = max(it->offset - sampleFrames, 0);
	}
	offset -= sampleFrames;
	assert(0 <= offset && offset < 16);
	return eventCount;*/
}

for (var i = 0; i < BALL_COUNT; ++i) {
	balls[i] = {
		active: false,
		x: 0.0,
		y: 0.0,
		dx: 0.0,
		dy: 0.0
	}
}
balls[0].active = true;
balls[0].dy = 1.0;
