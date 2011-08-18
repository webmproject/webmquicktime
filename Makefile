all:
	xcodebuild -target WebM -configuration Release

debug:
	xcodebuild -target WebM -configuration Debug

clean:
	xcodebuild -target WebM -configuration Release clean
	xcodebuild -target WebM -configuration Debug clean
