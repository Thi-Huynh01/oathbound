package com.oathbound.backend.service;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.stereotype.Service;
import org.springframework.web.client.RestClient;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.ObjectMapper;

import java.util.Map;

@Service
public class OpenAIService {
    private final RestClient restClient;
    private final ObjectMapper objectMapper = new ObjectMapper();

    public OpenAIService(@Value("${openai.api-key}") String apiKey) {
        this.restClient = RestClient.builder()
                .baseUrl("https://api.openai.com/v1")
                .defaultHeader(HttpHeaders.AUTHORIZATION, "Bearer " + apiKey)
                .defaultHeader(HttpHeaders.CONTENT_TYPE, MediaType.APPLICATION_JSON_VALUE)
                .build();
    }

    public String sendMessage(String message) throws Exception {

        Map<String, Object> requestBody = Map.of(
                "model", "gpt-6-luna",
                "input", message
        );

        String response = restClient.post()
                .uri("/responses")
                .body(requestBody)
                .retrieve()
                .body(String.class);

        JsonNode root = objectMapper.readTree(response);
        JsonNode output = root.get("output");

        if (output != null && output.isArray()) {
            for (JsonNode item : output) {

                if ("message".equals(item.path("type").asString())) {

                    JsonNode content = item.get("content");

                    if (content != null && content.isArray()) {
                        for (JsonNode part : content) {

                            if ("output_text".equals(part.path("type").asString())) {
                                return part.path("text").asString();
                            }
                        }
                    }
                }
            }
        }

        throw new RuntimeException("OpenAI did not return a text response.");
    }
}
