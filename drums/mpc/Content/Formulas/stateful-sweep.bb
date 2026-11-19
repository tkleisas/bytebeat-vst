// name: Stateful Sweep
// mode: bytebeat
// rate: 8000
// a: Decay
// b: Input
x = x*(0.99 + a*0.009) + (t>>8&15)*(0.5+b);
x*6
