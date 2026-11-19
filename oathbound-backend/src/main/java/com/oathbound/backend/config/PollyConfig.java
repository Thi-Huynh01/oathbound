package com.oathbound.backend.config;

import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import software.amazon.awssdk.auth.credentials.ProfileCredentialsProvider;
import software.amazon.awssdk.regions.Region;
import software.amazon.awssdk.services.polly.PollyClient;

@Configuration
public class PollyConfig {

    @Bean
    public PollyClient pollyClient() {
        return PollyClient.builder()
                .region(Region.US_EAST_1)
                .credentialsProvider(
                        ProfileCredentialsProvider.create("oathbound")
                )
                .build();
    }
}
