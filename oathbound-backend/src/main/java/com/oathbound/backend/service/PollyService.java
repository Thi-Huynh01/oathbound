package com.oathbound.backend.service;

import org.springframework.stereotype.Service;
import software.amazon.awssdk.core.sync.ResponseTransformer;
import software.amazon.awssdk.services.polly.PollyClient;
import software.amazon.awssdk.services.polly.model.*;

import java.nio.file.Path;

@Service
public class PollyService {
    private final PollyClient pollyClient;

    public PollyService(PollyClient pollyClient) {
        this.pollyClient = pollyClient;
    }

    public void synthesizeToFile(String text, Path outputPath) {

        SynthesizeSpeechRequest request = SynthesizeSpeechRequest.builder()
                .text(text)
                .textType(TextType.TEXT)
                .voiceId(VoiceId.RUTH)
                .engine(Engine.GENERATIVE)
                .outputFormat(OutputFormat.MP3)
                .build();

        pollyClient.synthesizeSpeech(
                request,
                ResponseTransformer.toFile(outputPath)
        );
    }
}
