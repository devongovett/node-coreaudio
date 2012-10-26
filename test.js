var test = require('./build/Release/coreaudio');

var context = new test.JSAudioContext(4096, 44100);

var x = 0;
var freq = context.sampleRate / (440 * 2 * Math.PI);

context.onaudioprocess = function(buffer) {
    // console.log(buffer.length);
    var offset = 0;
    for (var i = 0; i < context.bufferSize; i++) {
        // buffer[i] = buffer[i + 1] = Math.sin(x++ / freq);
        buffer.writeFloatLE(Math.sin(x / freq), offset); offset += 4;
        buffer.writeFloatLE(Math.sin(x / freq), offset); offset += 4;
        x++;
    }
}

console.log(context);

context.start();