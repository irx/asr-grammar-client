#include "RemoteSession.h"

#include <cstring>

#include <grpc++/client_context.h>
#include <grpc++/create_channel.h>

namespace sarmata
{

RemoteSession::RemoteSession(const std::string & host)
    : host_(host)
    , samplesStreamCompleted_(false)
{}

RemoteSession::~RemoteSession()
{
    if (stream_)
    {
		if(!samplesStreamCompleted_)
		{
			stream_->WritesDone();
		}
        stream_->Finish();
    }
}

void RemoteSession::PreDefineGrammar(const std::string & grammarName, const std::string & grammarData)
{
    DefineGrammarRequest request;
    request.set_name(grammarName);
    request.set_grammar(grammarData);

    DefineGrammarResponse response;

    auto stub = ASR::NewStub(grpc::CreateChannel(host_, grpc::InsecureChannelCredentials()));
    grpc::ClientContext context;
    const auto status = stub->DefineGrammar(&context, request, &response);

    if (!status.ok())
    {
        throw std::runtime_error(std::string("DefineGrammar error: ") + status.error_message());
    }

    if (!response.ok())
    {
        throw std::runtime_error(std::string("Grammar not created: ") + response.error());
    }
}

void RemoteSession::Open(const std::string & token, const ASRSessionSettings & settings)
{
    stub_ = ASR::NewStub(grpc::CreateChannel(host_, grpc::InsecureChannelCredentials()));
    stream_ = stub_->Recognize(&context_);
    samplesStreamCompleted_ = false;
    
    InitialRecognizeRequest initial;
    for (const auto & field : settings)
    {
        auto * configField = initial.add_config();
        configField->set_key(field.first);
        configField->set_value(field.second);
    }
    RecognizeRequest request;
    *request.mutable_initial_request() = initial;
    bool ok = stream_->Write(request);
    if (!ok)
    {
        throw std::runtime_error("Stream closed");  //todo: add own exception hierarchy
    }
}

void RemoteSession::AddSamples(const std::vector<short> & data)
{
    if(samplesStreamCompleted_)
    {
        throw std::runtime_error("Stream closed");
    }
	
    const auto chunk_size = 3*1024*1024/sizeof(short);   // less then size in https://github.com/grpc/grpc/blob/v1.0.x/src/core/lib/surface/channel.c#L84
    std::vector<short> chunk;
    chunk.reserve(chunk_size);
    for (int i = 0; i < data.size(); i++)
    {
        chunk.push_back(data[i]);
        if (chunk.size() == chunk_size)
        {
            sendSamples(chunk);
            chunk.clear();
        }
    }
    if (chunk.size() != 0)
    {
        sendSamples(chunk);
    }
}


void RemoteSession::sendSamples(const std::vector<short> & data)
{
    AudioRequest audio;
    std::string content(data.size() * sizeof(short), 0);
    std::memcpy( (char*)content.data(), data.data(), content.size());
    audio.set_content(content);
    audio.set_end_of_stream(false);
    
    RecognizeRequest request;
    *request.mutable_audio_request() = audio;
    bool ok = stream_->Write(request);
    if (!ok)
    {
        throw std::runtime_error("Stream closed");  //todo: add own exception hierarchy
    }
}

void RemoteSession::EndOfStream()
{
    if(samplesStreamCompleted_)
    {
        throw std::runtime_error("Stream closed");
    }
	
    AudioRequest audio;
    audio.set_end_of_stream(true);
    RecognizeRequest request;
    *request.mutable_audio_request() = audio;
    bool ok = stream_->Write(request);
    if (!ok)
    {
        throw std::runtime_error("Stream closed");  //todo: add own exception hierarchy
    }
	
    //closing stream
    stream_->WritesDone();
    samplesStreamCompleted_ = true;

}

RecognizeResponse RemoteSession::WaitForResponse(void)
{
    if (!stream_)
    {
        return RecognizeResponse();
    }
    RecognizeResponse response;
    bool ok = stream_->Read(&response);
    if (!ok)
    {
        auto status = stream_->Finish();
        if (status.error_code() != grpc::OK)
        {
            throw std::runtime_error("Error while reading message: " + status.error_message());
        }
        else
        {
            stream_.reset();
        }
    }
    return response;
}

}
