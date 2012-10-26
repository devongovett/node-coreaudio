{
    "targets": [{
        "target_name": "coreaudio",
        "sources": ["context.cc"],
        "conditions": [
            ['OS=="mac"', {
                "link_settings": {
                    "libraries": ["AudioUnit.framework"]
                }
            }]
        ]
    }]
}