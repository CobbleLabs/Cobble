"use client";

import { Heading, Text } from "./Typography";

interface FAQItemProps {
    question: string;
    answer: string;
}

export function FAQItem({ question, answer }: FAQItemProps) {
    return (
        <div>
            <Heading className="text-base font-bold text-white mb-2">{question}</Heading>
            <Text className="text-sm leading-relaxed">{answer}</Text>
        </div>
    );
}
