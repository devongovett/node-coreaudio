#ifndef CONTEXT_H
#define CONTEXT_H

#include <uv.h>
#include <node.h>
#include <node_buffer.h>
#include <v8.h>
#include <AudioUnit/AudioUnit.h>

using namespace v8;
using namespace node;

class JSAudioContext : ObjectWrap {
    public:
        static void Init(Handle<Object> target);
        
    private:
        JSAudioContext() {};
        ~JSAudioContext() {};
        
        static Handle<Value> New(const Arguments& args);
        static Handle<Value> GetOnAudioProcess(Local<String> property, const AccessorInfo& info);
        static void SetOnAudioProcess(Local<String> property, Local<Value> value, const AccessorInfo& info);
        static Handle<Value> Start(const Arguments& args);
        static Handle<Value> Stop(const Arguments& args);
        
        static void ReloadAudioData(uv_async_t *handle, int status);
        static OSStatus AudioUnitCallback(
            void *inRefCon, 
            AudioUnitRenderActionFlags *ioActionFlags,
            const AudioTimeStamp *inTimeStamp,
            UInt32 inBusNumber,
            UInt32 inNumberFrames,
            AudioBufferList *ioData
        );
                
        int sampleRate;        
        int channels;
        size_t bufferSize;
        bool isPlaying;
        Persistent<Function> onaudioprocess;
        
        AudioUnit audioUnit;
        uv_async_t notifier;
        
        Buffer *audioData;
        uint dataOffset;
        
        char *tail;
        uint tailSize;
        uint tailOffset;
};

#endif
