#define BUILDING_NODE_EXTENSION
#include "context.h"

// Make Node module
extern "C" void init(Handle<Object> target) {
    JSAudioContext::Init(target);
}

NODE_MODULE(hello, init)

// Sets up the class
void JSAudioContext::Init(Handle<Object> target) {
    // Constructor
    Local<FunctionTemplate> tpl = FunctionTemplate::New(New);
    tpl->SetClassName(String::NewSymbol("JSAudioContext"));
    
    Local<ObjectTemplate> instance = tpl->InstanceTemplate();
    instance->SetInternalFieldCount(1);
    
    instance->SetAccessor(String::NewSymbol("onaudioprocess"), 
                            JSAudioContext::GetOnAudioProcess, 
                            JSAudioContext::SetOnAudioProcess);
    
    // Prototype
    tpl->PrototypeTemplate()->Set(String::NewSymbol("start"),
        FunctionTemplate::New(Start)->GetFunction());
        
    Persistent<Function> constructor = Persistent<Function>::New(tpl->GetFunction());
    target->Set(String::NewSymbol("JSAudioContext"), constructor);
}

// Constructor
Handle<Value> JSAudioContext::New(const Arguments& args) {
    HandleScope scope;
    
    JSAudioContext *context = new JSAudioContext();
    context->isPlaying = false;
    
    // Describe audio unit
    AudioComponentDescription description;
    description.componentType = kAudioUnitType_Output;
    description.componentSubType = kAudioUnitSubType_DefaultOutput;
    description.componentManufacturer = kAudioUnitManufacturer_Apple;
    
    // Get component
    AudioComponent component = AudioComponentFindNext(NULL, &description);
    AudioComponentInstanceNew(component, &context->audioUnit);
    
    // bufferSize argument
    if (args.Length() >= 1 && args[0]->IsNumber()) {
        context->bufferSize = args[0]->NumberValue();
    } else {
        context->bufferSize = 1024;
    }
    
    // sampleRate argument
    if (args.Length() >= 2 && args[1]->IsNumber()) {
        context->sampleRate = args[1]->NumberValue();
    } else {
        Float64 sampleRate;
        uint size = sizeof(sampleRate);
        AudioUnitGetProperty(context->audioUnit, kAudioUnitProperty_SampleRate,
            kAudioUnitScope_Output, 0, &sampleRate, &size);
            
        context->sampleRate = (uint)sampleRate;
    }
    
    // numChannels argument
    if (args.Length() >= 3 && args[2]->IsNumber()) {
        context->channels = args[2]->NumberValue();
    } else {
        context->channels = 2;
    }
    
    context->audioData = Buffer::New(context->channels * context->bufferSize * sizeof(float));
    context->dataOffset = 0;
    
    context->tail = (char*)malloc(context->channels * context->bufferSize * sizeof(float));
    context->tailSize = 0;
    context->tailOffset = 0;
    
    // setup the callback
    AURenderCallbackStruct callback;
    callback.inputProc = JSAudioContext::AudioUnitCallback;
    callback.inputProcRefCon = context;
    
    AudioUnitSetProperty(context->audioUnit, kAudioUnitProperty_SetRenderCallback,
        kAudioUnitScope_Input, 0, &callback, sizeof(AURenderCallbackStruct));
                         
    // setup the format
    AudioStreamBasicDescription audioFormat;
    audioFormat.mSampleRate = context->sampleRate;
    audioFormat.mFormatID = kAudioFormatLinearPCM;
    audioFormat.mFormatFlags = kAudioFormatFlagsAudioUnitCanonical;
    audioFormat.mChannelsPerFrame = context->channels;
    audioFormat.mBytesPerPacket = sizeof(AudioUnitSampleType);
    audioFormat.mBytesPerFrame = sizeof(AudioUnitSampleType);
    audioFormat.mFramesPerPacket = 1;
    audioFormat.mBitsPerChannel = 8 * sizeof(AudioUnitSampleType);
    audioFormat.mReserved = 0;
    
    AudioUnitSetProperty(context->audioUnit, kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input, 0, &audioFormat, sizeof(audioFormat));
                      
    AudioUnitInitialize(context->audioUnit);
                         
    // expose fields to JavaScript
    args.This()->Set(String::NewSymbol("sampleRate"), Integer::New(context->sampleRate), PropertyAttribute(ReadOnly|DontDelete));
    args.This()->Set(String::NewSymbol("channels"), Integer::New(context->channels), PropertyAttribute(ReadOnly|DontDelete));
    args.This()->Set(String::NewSymbol("bufferSize"), Integer::New(context->bufferSize), PropertyAttribute(ReadOnly|DontDelete));
    
    context->Wrap(args.This());
    return args.This();
}

// Gets the onaudioprocess function
Handle<Value> JSAudioContext::GetOnAudioProcess(Local<String> property, const AccessorInfo& info) {
    JSAudioContext *context = ObjectWrap::Unwrap<JSAudioContext>(info.Holder());
    return context->onaudioprocess;
}

// Sets the onaudioprocess function
void JSAudioContext::SetOnAudioProcess(Local<String> property, Local<Value> value, const AccessorInfo& info) {
    if (!value->IsFunction())
        ThrowException(Exception::TypeError(String::New("onaudioprocess must be a function.")));
    
    JSAudioContext *context = ObjectWrap::Unwrap<JSAudioContext>(info.Holder());
    context->onaudioprocess.Dispose();
    
    Local<Function> func = Local<Function>::Cast(value);
    context->onaudioprocess = Persistent<Function>::New(func);
}

// Starts the AudioUnit and the onaudioprocess callback firing
Handle<Value> JSAudioContext::Start(const Arguments& args) {
    HandleScope scope;
    
    JSAudioContext *context = ObjectWrap::Unwrap<JSAudioContext>(args.This());
    if (!context->isPlaying) {
        context->isPlaying = true;
        
        context->notifier.data = context;
        uv_async_init(uv_default_loop(), &context->notifier, JSAudioContext::ReloadAudioData);
        uv_async_send(&context->notifier);
        
        AudioOutputUnitStart(context->audioUnit);
    }
    
    return scope.Close(Undefined());
}

// Calls the JavaScript onaudioprocess function to refill the buffer
void JSAudioContext::ReloadAudioData(uv_async_t *handle, int status) {
    HandleScope scope;
    JSAudioContext *context = static_cast<JSAudioContext*>(handle->data);
        
    if (!context->onaudioprocess.IsEmpty()) {
        Buffer *audioData = context->audioData;
        memset(Buffer::Data(audioData), 0, Buffer::Length(audioData));
        context->dataOffset = 0;
        
        Handle<Value> args[] = { audioData->handle_ };
        context->onaudioprocess->Call(Context::GetCurrent()->Global(), 1, args);
    }
}

void CopyBuffer(uint inNumberFrames, int channels, float *in, AudioBufferList *ioData, int offset) {
    for (uint frame = 0; frame < inNumberFrames; frame++) {
        for (int ch = 0; ch < channels; ch++) {
            float *out = (float*)ioData->mBuffers[ch].mData;
            out[frame + offset] = *in++;
        }
    }
}

// The AudioUnit callback
OSStatus JSAudioContext::AudioUnitCallback(
    void *inRefCon, 
    AudioUnitRenderActionFlags *ioActionFlags,
    const AudioTimeStamp *inTimeStamp,
    UInt32 inBusNumber,
    UInt32 inNumberFrames,
    AudioBufferList *ioData
) {
    JSAudioContext *context = static_cast<JSAudioContext*>(inRefCon);
    
    float *in;
    uint origFrames = inNumberFrames;
        
    if (context->tailSize > 0) {
        in = (float*)context->tail + (context->tailOffset * context->channels);
        uint numFrames = inNumberFrames < context->tailSize ? inNumberFrames : context->tailSize;
        
        CopyBuffer(numFrames, context->channels, in, ioData, 0);
        
        inNumberFrames -= numFrames;
        context->tailSize -= numFrames;
        context->tailOffset += numFrames;
    }
        
    if (inNumberFrames > 0) {
        in = (float*)Buffer::Data(context->audioData) + (context->dataOffset * context->channels);
        CopyBuffer(inNumberFrames, context->channels, in, ioData, origFrames - inNumberFrames);
        
        in += inNumberFrames * context->channels;
        context->dataOffset += inNumberFrames;
        
        if (context->dataOffset > context->bufferSize - origFrames) {
            context->tailSize = context->bufferSize - context->dataOffset;
            context->tailOffset = 0;
        
            memcpy(context->tail, in, context->tailSize * context->channels * sizeof(float));
        
            uv_async_send(&context->notifier);
        }
    }
    
    return noErr;
}
