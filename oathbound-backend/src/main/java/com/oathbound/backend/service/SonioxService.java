package com.oathbound.backend.service;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.MediaType;
import org.springframework.stereotype.Service;
import org.springframework.util.LinkedMultiValueMap;
import org.springframework.util.MultiValueMap;
import org.springframework.web.client.RestClient;
import org.springframework.web.multipart.MultipartFile;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.ObjectMapper;
import java.util.Map;

@Service
public class SonioxService {
    private final RestClient restClient;
    private final ObjectMapper objectMapper = new ObjectMapper();

    public SonioxService(@Value("${soniox.api-key}") String apiKey) {
        this.restClient = RestClient.builder()
                .baseUrl("https://api.soniox.com/v1")
                .defaultHeader("Authorization", "Bearer " + apiKey)
                .build();
    }

    public String uploadAudio(MultipartFile file) {
        MultiValueMap<String, Object> body = new LinkedMultiValueMap<>();
        body.add("file", file.getResource());

        return restClient.post()
                .uri("/files")
                .contentType(MediaType.MULTIPART_FORM_DATA)
                .body(body)
                .retrieve()
                .body(String.class);
    }

    public String createTranscription(String fileId) {
        Map<String, String> requestBody = Map.of(
                "model", "stt-async-v6",
                "file_id", fileId
        );

        return restClient.post()
                .uri("/transcriptions")
                .contentType(MediaType.APPLICATION_JSON)
                .body(requestBody)
                .retrieve()
                .body(String.class);
    }

    public String uploadAndTranscribe(MultipartFile file) throws Exception {

        String uploadResponse = uploadAudio(file);

        JsonNode uploadJson = objectMapper.readTree(uploadResponse);
        String fileId = uploadJson.get("id").asString();

        String transcriptionResponse = createTranscription(fileId);

        JsonNode transcriptionJson = objectMapper.readTree(transcriptionResponse);
        String transcriptionId = transcriptionJson.get("id").asString();

        int maxAttempts = 30;

        for (int attempt = 0; attempt < maxAttempts; attempt++) {

            String statusResponse = restClient.get()
                    .uri("/transcriptions/{id}", transcriptionId)
                    .retrieve()
                    .body(String.class);

            JsonNode statusJson = objectMapper.readTree(statusResponse);
            String status = statusJson.get("status").asString();

            System.out.println("Soniox transcription status: " + status);

            if ("completed".equals(status)) {
                break;
            }

            if ("error".equals(status)) {
                throw new RuntimeException(
                        "Soniox transcription failed: " +
                                statusJson.get("error_message").asString()
                );
            }

            if (attempt == maxAttempts - 1) {
                throw new RuntimeException("Soniox transcription timed out.");
            }

            try {
                Thread.sleep(1000);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                throw new RuntimeException("Transcription polling was interrupted.", e);
            }
        }

        return restClient.get()
                .uri("/transcriptions/{id}/transcript", transcriptionId)
                .retrieve()
                .body(String.class);
    }

    public String extractTranscript(String transcriptionResponse) throws Exception {

        JsonNode root = objectMapper.readTree(transcriptionResponse);

        StringBuilder transcript = new StringBuilder();

        JsonNode tokens = root.get("tokens");

        if (tokens != null && tokens.isArray()) {
            for (JsonNode token : tokens) {
                String text = token.get("text").asString();
                transcript.append(text);
            }
        }

        return transcript.toString().trim();
    }

}
