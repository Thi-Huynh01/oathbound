package com.oathbound.backend.controller;

import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RestController;
import software.amazon.awssdk.services.s3.S3Client;
import software.amazon.awssdk.services.s3.model.ListObjectsV2Request;
import software.amazon.awssdk.services.s3.model.ListObjectsV2Response;

@RestController
public class S3Controller {

    private final S3Client s3Client;

    private static final String BUCKET_NAME= "oathbound-audio-assets";

    public S3Controller(S3Client s3Client) {
        this.s3Client = s3Client;
    }

    @GetMapping("/api/s3/test")
    public String testS3Connection() {
        ListObjectsV2Response response = s3Client.listObjectsV2(
                ListObjectsV2Request.builder()
                        .bucket(BUCKET_NAME)
                        .build()
        );
        return "Successfully connected to S3! Objects Found: " + response.keyCount();
    }
}
