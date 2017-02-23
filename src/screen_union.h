class ScreenUnion : public Screen {
 public:
  void AddScreen(std::unique_ptr<Screen> screen) {
    screens_.push_back(std::move(screen));
  }

  bool Empty() const {
    return screens_.empty();
  };

 private:
  std::list<std::unique_ptr<Screen>> screens_;
};
