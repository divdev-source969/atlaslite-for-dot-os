// Example AtlasLight script.
// Copy to ~/.config/atlaslight/scripts/my-script.js
// Scripts run once at startup and customise the BROWSER. They do not run inside web pages.
//
//   atlas.block("ads.example.com")   add an ad-block rule (same formats as blocklist.txt)
//   atlas.css("body{...}")           CSS added to every page
//   atlas.log("text")                print to the terminal

atlas.log("hello from a user script");

// Block an extra domain
atlas.block("annoying-ads.example.com");

// Slightly bigger text and narrower lines on every page
atlas.css("body{font-size:17px;line-height:1.5;max-width:860px;margin-left:auto;margin-right:auto}");
