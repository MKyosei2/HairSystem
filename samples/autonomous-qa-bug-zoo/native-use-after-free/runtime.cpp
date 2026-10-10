struct Node { int value; };
int read_after_free() {
    Node* node = new Node{7};
    delete node;
    return node->value; // intentional Bug Zoo use-after-free
}
