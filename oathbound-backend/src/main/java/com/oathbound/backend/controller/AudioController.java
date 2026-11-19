package com.oathbound.backend.controller;

import com.oathbound.backend.service.OpenAIService;
import com.oathbound.backend.service.PollyService;
import com.oathbound.backend.service.SonioxService;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.multipart.MultipartFile;
import software.amazon.awssdk.services.s3.S3Client;
import software.amazon.awssdk.services.s3.model.GetObjectRequest;
import software.amazon.awssdk.services.s3.presigner.S3Presigner;
import software.amazon.awssdk.services.s3.presigner.model.GetObjectPresignRequest;
import software.amazon.awssdk.services.s3.presigner.model.PresignedGetObjectRequest;
import software.amazon.awssdk.core.sync.RequestBody;
import software.amazon.awssdk.services.s3.model.PutObjectRequest;

import java.nio.file.Files;
import java.util.UUID;
import java.util.Map;
import java.nio.file.Path;
import java.time.Duration;

@RestController
public class AudioController {

    private final S3Presigner s3Presigner;
    private final SonioxService sonioxService;
    private final OpenAIService openAIService;
    private final PollyService pollyService;
    private final S3Client s3Client;

    private static final String BUCKET_NAME = "oathbound-audio-assets";

    public AudioController(S3Presigner s3Presigner, SonioxService sonioxService, OpenAIService openAIService, PollyService pollyService, S3Client s3Client) {

        this.s3Presigner = s3Presigner;
        this.sonioxService = sonioxService;
        this.openAIService = openAIService;
        this.pollyService = pollyService;
        this.s3Client = s3Client;

    }

    @GetMapping("/api/audio/recording-url")
    public String getRecordingUrl() {

        GetObjectRequest getObjectRequest = GetObjectRequest.builder()
                .bucket(BUCKET_NAME)
                .key("recording.mp3")
                .build();

        GetObjectPresignRequest presignRequest = GetObjectPresignRequest.builder()
                .signatureDuration(Duration.ofMinutes(5))
                .getObjectRequest(getObjectRequest)
                .build();

        PresignedGetObjectRequest presignedRequest =
                s3Presigner.presignGetObject(presignRequest);

        return presignedRequest.url().toString();
    }

    @PostMapping(value="/api/audio/upload", consumes= MediaType.MULTIPART_FORM_DATA_VALUE)
    public ResponseEntity<Map<String, String>> uploadRecording(@RequestParam("file") MultipartFile file) throws Exception {

        if (file.isEmpty()) {
            return ResponseEntity.badRequest().body(Map.of(
                    "error","No recording received")
            );
        }

        // transcribe audio with Soniox
        String transcriptionResponse = sonioxService.uploadAndTranscribe(file);

        // extract the recognized words
        String transcript = sonioxService.extractTranscript(transcriptionResponse);
        
        // send recognized speech to OpenAI
        String aiResponse = openAIService.sendMessage(
                "You are Power Battery, a Green Lantern-inspired AI assistant. Do not use emojis." +
                        "The user said: " + transcript
        );

        Path outputPath = Files.createTempFile(
                "oathbound-response-",
                ".mp3"
        );

        Files.delete(outputPath);

        try {
            pollyService.synthesizeToFile(aiResponse, outputPath);

            // Upload the generated MP3 to S3
            String objectKey = "responses/" + UUID.randomUUID() + ".mp3";

            PutObjectRequest putRequest = PutObjectRequest.builder()
                    .bucket(BUCKET_NAME)
                    .key(objectKey)
                    .contentType("audio/mpeg")
                    .build();

            s3Client.putObject(
                    putRequest,
                    RequestBody.fromFile(outputPath)
            );

            // Generate a temporary download URL
            GetObjectRequest getObjectRequest = GetObjectRequest.builder()
                    .bucket(BUCKET_NAME)
                    .key(objectKey)
                    .build();

            GetObjectPresignRequest presignRequest = GetObjectPresignRequest.builder()
                    .signatureDuration(Duration.ofMinutes(5))
                    .getObjectRequest(getObjectRequest)
                    .build();

            PresignedGetObjectRequest presignedRequest =
                    s3Presigner.presignGetObject(presignRequest);

            String audioUrl = presignedRequest.url().toString();

            return ResponseEntity.ok(Map.of(
                    "response", aiResponse,
                    "audio_url", audioUrl
            ));

        } finally {
            Files.deleteIfExists(outputPath);
        }

    }

    @GetMapping("/api/audio/test-polly")
    public ResponseEntity<String> testPolly() {
        String testText = "Power Battery online. Welcome, Green Lantern of Sector 2 8 1 4";
        Path outputPath = Path.of("oathbound-test.mp3");
        pollyService.synthesizeToFile(testText, outputPath);

        return ResponseEntity.ok("Speech generated successfully: " + outputPath.toAbsolutePath());
    }
}
