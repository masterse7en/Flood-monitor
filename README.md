# Flood Monitor

Solar-powered flood level monitor built on an ESP32-S3 with an ultrasonic sensor
and camera, reporting to a serverless AWS backend.

## Architecture

```mermaid
flowchart LR
    subgraph FIELD["📍 Field node · Kozhikode"]
        SUN["☀️ Solar panel 6 V"] --> BAT["🔋 2× 18650 + TP4056"]
        BAT --> ESP["ESP32-S3 N16R8"]
        SONAR["📏 JSN-SR04T<br/>water distance"] --> ESP
        CAM["📷 OV3660<br/>target photos"] --> ESP
    end

    subgraph AWS["☁️ AWS · us-east-1"]
        INGEST["λ flood_ingest"]
        PHOTO["λ flood_photo_upload"]
        DDB[("DynamoDB<br/>FloodReadings")]
        SNS["SNS<br/>flood-alerts"]
        S3[("S3<br/>photo bucket")]
        INGEST -->|"every reading"| DDB
        INGEST -->|"state change"| SNS
        PHOTO --> S3
    end

    ESP -->|"HTTPS · reading JSON<br/>x-device-key"| INGEST
    ESP -->|"HTTPS · JPEG<br/>x-device-key"| PHOTO
    SNS --> MAIL["📧 Email alert"]

    classDef device fill:#1f6feb,stroke:#58a6ff,color:#fff
    classDef power fill:#9e6a03,stroke:#d29922,color:#fff
    classDef lambda fill:#d97706,stroke:#f59e0b,color:#fff
    classDef store fill:#238636,stroke:#3fb950,color:#fff
    classDef alert fill:#a40e26,stroke:#f85149,color:#fff
    class ESP,SONAR,CAM device
    class SUN,BAT power
    class INGEST,PHOTO lambda
    class DDB,S3 store
    class SNS,MAIL alert
```

## Phases

| Phase | Scope | Status |
|---|---|---|
| 1 | Distance monitoring, photos, DynamoDB, SNS alerts, S3 upload | Done |
| 2 | Weather API: device runs only when rain or storm alerts are active | Planned |
| 3 | Decision model: rain confidence decides whether to keep monitoring | Planned |

## Repository layout

```
firmware/        ESP32-S3 Arduino sketch
hardware/        Enclosure CAD, STL, renders and wiring
lambdas/         AWS Lambda functions (Python 3.12)
decision_model/  Phase 3
iam/             Least-privilege inline policies
docs/            Setup and notes for each phase
```

## Enclosure

![Enclosure render](hardware/enclosure/renders/enclosure.png)

Weatherproof case with solar panel roof. See hardware/enclosure/.

## Hardware

- ESP32-S3 N16R8 with OV3660 camera
- JSN-SR04T waterproof ultrasonic sensor (TRIG GPIO 1, ECHO GPIO 2, 3.3 V)
- Range 25 to 450 cm

## Security

- Secrets live in firmware/aws_distance_camera/secrets.h, which is git-ignored
- Function URLs reject requests without the shared device key
- Each Lambda role has only the permissions it needs (see iam/)

## Setup

See docs/phase1-monitoring.md.
