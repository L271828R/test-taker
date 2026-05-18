#include "personality_lib.h"

std::map<std::string, std::vector<std::string>> DefaultPersonalityLibrary() {
    return {
        {"Programming", {"Bill Gates", "Steve Jobs", "Linus Torvalds", "Ada Lovelace"}},
        {"Science",     {"Albert Einstein", "Richard Feynman", "Marie Curie", "Carl Sagan"}},
        {"Literature",  {"Edgar Allan Poe", "Agatha Christie", "Mark Twain"}},
        {"Philosophy",  {"Socrates", "Aristotle", "Nietzsche"}},
    };
}
