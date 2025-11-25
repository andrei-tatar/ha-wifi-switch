const GAMMA = 2.5;

const MIN = 6.5e-3; //sec
const MAX = 0e-3; //sec
const TIMER = 1e6; //Hz
const TICKS_PER_10ms = 59247;

const brightness = new Array(100).fill(0).map((_, i) => i + 1);
const ticksMin = TIMER * MIN;
const ticksMax = TIMER * MAX;

const ticksArray = [];
for (let i = 0; i < 100; i++) {
  //const corrected = i / 100; // linear
  const corrected = Math.pow(i, GAMMA) / Math.pow(99, GAMMA); // exponential

  const msec = (ticksMax - ticksMin) * corrected + ticksMin;
  const ticks = Math.round(msec); //Math.round((msec * TICKS_PER_10ms) / 10000);
  ticksArray[i] = ticks >= ticksArray[i - 1] ? ticksArray[i - 1] - 20 : ticks;
}

console.log(ticksArray.join(","));
